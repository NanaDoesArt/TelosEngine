#include "Object/Object.h"
#include "Logging/Log.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    std::thread::id GGameThreadID = std::this_thread::get_id();

    // The ONE object id counter in the process. It lives here (inside Core.dll) so every
    // module draws from the same sequence. A header-level 'inline static' would give each
    // DLL its own counter and produce colliding ids.
    std::atomic<uint64_t> GNextObjectID{ 1 };

    template <typename T>
    class TScopedValue
    {
    public:
        TScopedValue(T& InValue, T NewValue)
            : Value(InValue), Previous(InValue)
        {
            Value = NewValue;
        }

        ~TScopedValue()
        {
            Value = Previous;
        }

        TScopedValue(const TScopedValue&) = delete;
        TScopedValue& operator=(const TScopedValue&) = delete;

    private:
        T& Value;
        T Previous;
    };

    // True if Ancestor appears anywhere in Obj's outer chain
    bool IsOwnedBy(const RObject* Obj, const RObject* Ancestor)
    {
        for (const RObject* Current = Obj ? Obj->GetOuter() : nullptr; Current; Current = Current->GetOuter())
        {
            if (Current == Ancestor)
            {
                return true;
            }
        }
        return false;
    }
}

uint64_t RObject::AllocateUniqueID()
{
    return GNextObjectID.fetch_add(1, std::memory_order_relaxed);
}

// Everything the registry needs to know while one class default object is being constructed
struct FObjectRegistry::FCdoBuildContext
{
    FClassInfo* OwnerClass = nullptr;       // class whose CDO is being built
    RObject* Root = nullptr;                // the CDO itself (the first object registered during the build)
    std::vector<uint64_t> CreatedIDs;       // every other object created during the build, in creation order
};

FObjectRegistry& FObjectRegistry::Get()
{
    static FObjectRegistry Instance;
    return Instance;
}

void FObjectRegistry::BindGameThread()
{
    GGameThreadID = std::this_thread::get_id();
}

bool FObjectRegistry::IsOnGameThread() const
{
    return std::this_thread::get_id() == GGameThreadID;
}

void FObjectRegistry::CheckGameThread(const char* Operation) const
{
    if (!IsOnGameThread())
    {
        VENGINE_LOG(LogCore, Fatal, "[ObjectRegistry] Thread violation: '{}' called off the Game Thread!", Operation);
    }
}

bool FObjectRegistry::RegisterClass(
    const FString& Name,
    const FString& SuperName,
    const FString& ModuleName,
    TFunction<RObject*()> Factory,
    TFunction<void(RObject*)> CdoDeleter)
{
    CheckGameThread("RegisterClass");

    // Bootstrap classes must exist before any other class. Otherwise a module could register a
    // class that claims the name "RObject" first, and the real one would later be ignored as a
    // duplicate. Idempotent: the flag is set before the bootstrap registrations run.
    EnsureBootstrapClasses();

    if (!Factory)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Cannot register '{}': factory is empty.",
            Name.CStr()
        );
        return false;
    }

    // A class declared in a header is compiled into every module that includes it, so the same
    // class legitimately arrives more than once. The first registrant owns it, and the first
    // registrant is always the declaring module: including a header requires depending on the
    // module that owns it, and dependencies boot first.
    if (const FClassInfo* Existing = Classes.Find(Name))
    {
        if (Existing->SuperClassName == SuperName)
        {
            VENGINE_LOG(
                LogCore,
                Verbose,
                "[ObjectRegistry] '{}' already registered by '{}'; ignoring duplicate registration from '{}'.",
                Name.CStr(),
                Existing->OwningModule.CStr(),
                ModuleName.CStr()
            );
            return true;
        }

        // Same name, different base class: two different classes fighting over one name.
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Class name collision for '{}': module '{}' registered it with base '{}', module '{}' tried to register it with base '{}'.",
            Name.CStr(),
            Existing->OwningModule.CStr(),
            Existing->SuperClassName.IsEmpty() ? "None" : Existing->SuperClassName.CStr(),
            ModuleName.CStr(),
            SuperName.IsEmpty() ? "None" : SuperName.CStr()
        );
        return false;
    }

    FClassInfo NewClass;
    NewClass.Name = Name;
    NewClass.SuperClassName = SuperName;
    NewClass.OwningModule = ModuleName;
    NewClass.Factory = std::move(Factory);
    NewClass.CdoDeleter = std::move(CdoDeleter);

    Classes.Add(Name, eastl::move(NewClass));
    ResolveSuperClasses();

    FClassInfo* ClassInfo = Classes.Find(Name);

    FCdoBuildContext Context;
    Context.OwnerClass = ClassInfo;

    try
    {
        TScopedValue<FCdoBuildContext*> CdoScope(ActiveCdoContext, &Context);
        TScopedValue<FClassInfo*> ClassScope(ActiveConstructingClass, ClassInfo);

        // While the default object's constructor runs, RegisterInstance() sets
        // ActiveConstructionSite to the default object, so NewObject<T>() inside that constructor
        // parents to it. Restore the previous site here: a default object must never remain the
        // owner of objects created after registration finishes.
        TScopedValue<RObject*> SiteScope(ActiveConstructionSite, nullptr);

        ClassInfo->ClassDefaultObject = ClassInfo->Factory();
    }
    catch (...)
    {
        // C++ already destroyed the half-built CDO together with everything parented to it.
        // Objects it created WITHOUT parenting to it are still alive: destroy them before dropping the class metadata.
        DestroyUnownedDefaultSubobjects(Context, nullptr);
        Classes.Remove(Name);
        ResolveSuperClasses();
        throw;
    }

    // Every object created inside the CDO constructor must be owned by the CDO, otherwise nothing
    // would ever destroy it. With NewObject<T>() that is automatic; only an explicit foreign
    // outer can still end up here.
    if (DestroyUnownedDefaultSubobjects(Context, ClassInfo->ClassDefaultObject) > 0)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Registration of '{}' rejected: its default object constructor created objects that are not owned by the default object.",
            Name.CStr()
        );
        DestroyClassDefaultObject(*ClassInfo);
        Classes.Remove(Name);
        ResolveSuperClasses();
        return false;
    }

    if (!SuperName.IsEmpty() && !ClassInfo->SuperClass)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] '{}' (module '{}') declares base class '{}', which is not registered. "
            "The base module must boot first; check the module dependency.",
            Name.CStr(),
            ModuleName.CStr(),
            SuperName.CStr()
        );
    }

    VENGINE_LOG(
        LogCore,
        Info,
        "[ObjectRegistry] Registered: {} (Super: {}, Module: {})",
        Name.CStr(),
        SuperName.IsEmpty() ? "None" : SuperName.CStr(),
        ModuleName.CStr()
    );
    return true;
}

void FObjectRegistry::UnregisterClass(const FString& Name, const FString& ModuleName)
{
    CheckGameThread("UnregisterClass");

    FClassInfo* ClassInfo = Classes.Find(Name);
    if (!ClassInfo)
    {
        return;
    }

    // Header-defined classes are registered by the declaring module. Every other module has its
    // own ignored copy of the registrar, and must not remove metadata that is not theirs: that
    // would delete the class out from under the module that still uses it.
    if (ClassInfo->OwningModule != ModuleName)
    {
        return;
    }

    // Hard invariant: live instance records hold FClassInfo* pointers, so metadata must never be freed while instances survive
    auto FailIfInstancesRemain = [&]() -> bool
    {
        if (ClassInfo->LiveInstanceCount != 0)
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[ObjectRegistry] Cannot unregister '{}': {} live instances remain.",
                Name.CStr(),
                ClassInfo->LiveInstanceCount
            );
            return true;
        }
        return false;
    };

    if (FailIfInstancesRemain())
    {
        return;
    }

    // Default subobjects of this class that sit inside other classes' default objects:
    //  - same module: that module is unloading too, so we can release those default objects ourselves
    //    (no need for a perfect unregister order inside one module)
    //  - other module: that module still uses our code. Refuse.
    std::vector<FClassInfo*> SameModuleOwners;
    const FClassInfo* ForeignOwner = nullptr;
    if (ClassInfo->DefaultSubobjectCount != 0)
    {
        for (const auto& Pair : LiveInstances)
        {
            const FLiveInstanceRecord& Record = Pair.second;
            if (Record.Kind != EInstanceKind::DefaultSubobject || Record.ClassInfo != ClassInfo || !Record.OwnerClass)
            {
                continue;
            }

            if (Record.OwnerClass->OwningModule == ClassInfo->OwningModule)
            {
                if (std::find(SameModuleOwners.begin(), SameModuleOwners.end(), Record.OwnerClass) == SameModuleOwners.end())
                {
                    SameModuleOwners.push_back(Record.OwnerClass);
                }
            }
            else
            {
                ForeignOwner = Record.OwnerClass;
            }
        }
    }

    if (ForeignOwner)
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ObjectRegistry] Cannot unregister '{}': the default object of '{}' (module '{}') still owns {} object(s) of this class. Unload that module first.",
            Name.CStr(),
            ForeignOwner->Name.CStr(),
            ForeignOwner->OwningModule.CStr(),
            ClassInfo->DefaultSubobjectCount
        );
        return;
    }

    // Destroy default objects BEFORE removing any metadata. This also destroys the default subobjects they own.
    for (FClassInfo* Owner : SameModuleOwners)
    {
        DestroyClassDefaultObject(*Owner);
    }
    DestroyClassDefaultObject(*ClassInfo);

    // Anything still counted now was moved out of its default object, or is corrupted accounting
    if (FailIfInstancesRemain())
    {
        return;
    }
    if (ClassInfo->DefaultSubobjectCount != 0)
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ObjectRegistry] Corrupted default subobject accounting for '{}': {} remain after destroying default objects.",
            Name.CStr(),
            ClassInfo->DefaultSubobjectCount
        );
        return;
    }

    // A registered subclass with live objects would silently lose IsA()/Cast() support:
    // ResolveSuperClasses() would null its SuperClass while its instances are still walking
    // around with vtable pointers into our code. Treat it like every other destroyed-invariant
    // in this file: Fatal, and do not remove the metadata.
    for (const auto& Pair : Classes)
    {
        const FClassInfo& Other = Pair.second;
        if (Other.SuperClass == ClassInfo && (Other.LiveInstanceCount != 0 || Other.DefaultSubobjectCount != 0))
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[ObjectRegistry] Cannot unregister '{}': subclass '{}' still has live objects. Unload dependent modules first.",
                Name.CStr(),
                Other.Name.CStr()
            );
            return;
        }
    }

    Classes.Remove(Name);
    ResolveSuperClasses();
    VENGINE_LOG(LogCore, Info, "[ObjectRegistry] Unregistered: {}", Name.CStr());
}

void FObjectRegistry::DestroyClassDefaultObject(FClassInfo& Info)
{
    RObject* CDO = std::exchange(Info.ClassDefaultObject, nullptr);
    if (!CDO)
    {
        return;
    }

    if (Info.CdoDeleter)
    {
        Info.CdoDeleter(CDO);
    }
    else
    {
        delete CDO;
    }

    // A default subobject that was moved out of its default object (SetOuter) survives the CDO.
    // From now on it is an ordinary instance: it keeps blocking its class and no longer points at this owner.
    for (auto& Pair : LiveInstances)
    {
        FLiveInstanceRecord& Record = Pair.second;
        if (Record.Kind == EInstanceKind::DefaultSubobject && Record.OwnerClass == &Info)
        {
            Record.Kind = EInstanceKind::Ordinary;
            Record.OwnerClass = nullptr;
            --Record.ClassInfo->DefaultSubobjectCount;
            ++Record.ClassInfo->LiveInstanceCount;
        }
    }
}

int32_t FObjectRegistry::DestroyUnownedDefaultSubobjects(FCdoBuildContext& Context, const RObject* Root)
{
    int32_t Destroyed = 0;
    for (const uint64_t ID : Context.CreatedIDs)
    {
        const FLiveInstanceRecord* Record = LiveInstances.Find(ID);
        if (!Record)
        {
            continue; // already destroyed together with its owner
        }

        RObject* Object = Record->Instance;
        if (Root && IsOwnedBy(Object, Root))
        {
            continue;
        }

        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] '{}' was created while constructing the default object of '{}' but is not owned by it. Destroying it.",
            Record->ClassInfo->Name.CStr(),
            Context.OwnerClass->Name.CStr()
        );
        delete Object; // ~RObject unregisters it
        ++Destroyed;
    }
    return Destroyed;
}

void FObjectRegistry::ResolveSuperClasses()
{
    for (auto& Pair : Classes)
    {
        FClassInfo& Info = Pair.second;
        Info.SuperClass = !Info.SuperClassName.IsEmpty() ? FindClass(Info.SuperClassName) : nullptr;
    }
}

RObject* FObjectRegistry::ConstructInternal(FClassInfo* Info, RObject* InOuter)
{
    // A default object constructor that creates an object of its own class would recurse forever
    if (ActiveCdoContext && ActiveCdoContext->OwnerClass == Info)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] The default object constructor of '{}' tried to create another '{}'. This would recurse endlessly.",
            Info->Name.CStr(),
            Info->Name.CStr()
        );
        return nullptr;
    }

    RObject* Instance = nullptr;
    {
        TScopedValue<FClassInfo*> ClassScope(ActiveConstructingClass, Info);

        // RegisterInstance() sets ActiveConstructionSite to the new object while its constructor
        // body runs, so NewObject<T>() inside that constructor is owned by it. Restore the
        // enclosing site when construction ends (including when it throws).
        TScopedValue<RObject*> SiteScope(ActiveConstructionSite, nullptr);

        Instance = Info->Factory();
    }

    if (Instance && InOuter)
    {
        Instance->SetOuter(InOuter);
    }

    return Instance;
}

RObject* FObjectRegistry::ConstructObjectOfType(const FString& Name, RObject* InOuter, const FClassInfo* RequiredBaseClass)
{
    CheckGameThread("ConstructObjectOfType");

    // Every object must have an owner. This is the compile-time rule (no default argument on
    // NewObject) and this is the runtime backstop for hand-rolled calls.
    if (!InOuter)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Refusing to construct '{}' without an outer. NewObject<T>() picks an owner from context; nothing may be created without one.",
            Name.CStr()
        );
        return nullptr;
    }

    if (!RequiredBaseClass)
    {
        VENGINE_LOG(LogCore, Error, "[ObjectRegistry] Typed construction rejected for '{}': target base class is not registered.", Name.CStr());
        return nullptr;
    }

    FClassInfo* Info = Classes.Find(Name);
    if (!Info || !Info->Factory)
    {
        VENGINE_LOG(LogCore, Error, "[ObjectRegistry] Failed to construct unknown class: {}", Name.CStr());
        return nullptr;
    }

    if (!Info->IsChildOf(RequiredBaseClass))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Type mismatch: requested '{}' is not a child of '{}'.",
            Name.CStr(),
            RequiredBaseClass->Name.CStr()
        );
        return nullptr;
    }

    return ConstructInternal(Info, InOuter);
}

RObject* FObjectRegistry::ConstructObjectByName(const FString& Name, RObject* InOuter)
{
    CheckGameThread("ConstructObjectByName");

    if (!InOuter)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Refusing to construct '{}' without an outer. NewObject<T>() picks an owner from context; nothing may be created without one.",
            Name.CStr()
        );
        return nullptr;
    }

    FClassInfo* Info = Classes.Find(Name);
    if (!Info || !Info->Factory)
    {
        VENGINE_LOG(LogCore, Error, "[ObjectRegistry] Failed to construct unknown class: {}", Name.CStr());
        return nullptr;
    }

    return ConstructInternal(Info, InOuter);
}

const RObject* FObjectRegistry::GetCDO(const FString& Name) const
{
    CheckGameThread("GetCDO");
    const FClassInfo* Info = Classes.Find(Name);
    return Info ? Info->ClassDefaultObject : nullptr;
}

const FClassInfo* FObjectRegistry::FindClass(const FString& Name) const
{
    CheckGameThread("FindClass");
    return Classes.Find(Name);
}

// ==============================================================================
// Interfaces
// ==============================================================================

void FObjectRegistry::AddInterface(const FString& ClassName, FImplementedInterface Entry)
{
    CheckGameThread("AddInterface");

    FClassInfo* Info = Classes.Find(ClassName);
    if (!Info || !Entry.Name || !Entry.Thunk)
    {
        return;
    }

    // Same class, registered again from another module that includes its header. Keep the entry
    // that is already there: its thunk lives in the module that owns the class, so it cannot be
    // replaced by code from a module that may unload while the class still exists.
    if (Info->FindInterface(Entry.Name))
    {
        VENGINE_LOG(
            LogCore,
            Verbose,
            "[ObjectRegistry] '{}' already exposes interface '{}'; keeping the existing thunk.",
            ClassName.CStr(),
            Entry.Name
        );
        return;
    }

    Info->Interfaces.Add(Entry);
}

void* FObjectRegistry::GetInterfaceAddress(RObject* Object, const char* InterfaceName) const
{
    if (!Object || !InterfaceName)
    {
        return nullptr;
    }

    // Walk the class chain: an interface implemented by a base class is also implemented by every
    // subclass, and the thunk found on the base is still correct for a subclass instance because
    // the instance really is an instance of that base.
    for (const FClassInfo* Info = Object->GetClass(); Info; Info = Info->SuperClass)
    {
        if (const FImplementedInterface* Entry = Info->FindInterface(InterfaceName))
        {
            return Entry->Thunk(Object);
        }
    }

    return nullptr;
}

bool FObjectRegistry::HasLiveInstancesForModule(const FString& ModuleName, int32_t* OutCount) const
{
    CheckGameThread("HasLiveInstancesForModule");

    int32_t Total = 0;
    bool bHasDefaultSubobjects = false;
    for (const auto& Pair : Classes)
    {
        if (Pair.second.OwningModule == ModuleName)
        {
            Total += Pair.second.LiveInstanceCount;
            bHasDefaultSubobjects = bHasDefaultSubobjects || Pair.second.DefaultSubobjectCount != 0;
        }
    }

    // Default subobjects only block the module if another module's default object holds them
    if (bHasDefaultSubobjects)
    {
        for (const auto& Pair : LiveInstances)
        {
            const FLiveInstanceRecord& Record = Pair.second;
            if (Record.Kind == EInstanceKind::DefaultSubobject
                && Record.ClassInfo && Record.ClassInfo->OwningModule == ModuleName
                && (!Record.OwnerClass || Record.OwnerClass->OwningModule != ModuleName))
            {
                ++Total;
            }
        }
    }

    if (OutCount)
    {
        *OutCount = Total;
    }
    return Total > 0;
}

void FObjectRegistry::RegisterInstance(uint64_t UniqueID, RObject* Instance)
{
    CheckGameThread("RegisterInstance");

    FClassInfo* TargetClass = std::exchange(ActiveConstructingClass, nullptr);

    if (!TargetClass || UniqueID == 0 || !Instance)
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ObjectRegistry] Invalid object construction. Objects may only be created through NewObject<T>() or a registry factory."
        );
        return;
    }

    // The object carries its exact class for its whole lifetime, so ~RObject never needs a virtual
    // call or a name lookup, and the class can never be released while the vtable is still reachable.
    Instance->ClassInfo = TargetClass;

    // Everything created inside this object's constructor body from now on is owned by it.
    // ConstructInternal()/RegisterClass() restore the previous site when construction ends.
    ActiveConstructionSite = Instance;

    FLiveInstanceRecord Record;
    Record.Instance = Instance;
    Record.ClassInfo = TargetClass;

    if (ActiveCdoContext)
    {
        // The first object registered during a CDO build is the CDO itself. It is owned by FClassInfo and not counted.
        if (!ActiveCdoContext->Root)
        {
            ActiveCdoContext->Root = Instance;
            Record.Kind = EInstanceKind::ClassDefaultObject;
            LiveInstances.Add(UniqueID, Record);
            return;
        }

        // Everything else created during the build must end up owned by the CDO (checked after construction)
        Record.Kind = EInstanceKind::DefaultSubobject;
        Record.OwnerClass = ActiveCdoContext->OwnerClass;
        LiveInstances.Add(UniqueID, Record);
        ++TargetClass->DefaultSubobjectCount;
        ActiveCdoContext->CreatedIDs.push_back(UniqueID);
        return;
    }

    Record.Kind = EInstanceKind::Ordinary;
    LiveInstances.Add(UniqueID, Record);
    ++TargetClass->LiveInstanceCount;
}

void FObjectRegistry::UnregisterInstance(uint64_t UniqueID)
{
    CheckGameThread("UnregisterInstance");
    if (UniqueID == 0)
    {
        return;
    }

    FLiveInstanceRecord* Record = LiveInstances.Find(UniqueID);
    if (!Record)
    {
        return;
    }
    
    // If the object died while it was still the construction site (a constructor threw), nothing
    // may keep handing it out as an owner.
    if (Record->Instance == ActiveConstructionSite)
    {
        ActiveConstructionSite = nullptr;
    }

    int32_t* Counter = nullptr;
    if (Record->ClassInfo)
    {
        switch (Record->Kind)
        {
        case EInstanceKind::Ordinary:
            Counter = &Record->ClassInfo->LiveInstanceCount;
            break;
        case EInstanceKind::DefaultSubobject:
            Counter = &Record->ClassInfo->DefaultSubobjectCount;
            break;
        case EInstanceKind::ClassDefaultObject:
            break; // never counted
        }
    }

    if (Counter)
    {
        if (*Counter <= 0)
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[ObjectRegistry] Corrupted live instance accounting for '{}': count is already {}.",
                Record->ClassInfo->Name.CStr(),
                *Counter
            );
        }
        else
        {
            --(*Counter);
        }
    }
    LiveInstances.Remove(UniqueID);
}

RObject* FObjectRegistry::FindInstance(uint64_t UniqueID) const
{
    CheckGameThread("FindInstance");
    const FLiveInstanceRecord* Found = LiveInstances.Find(UniqueID);
    return Found ? Found->Instance : nullptr;
}

RObject* FObjectRegistry::GetConstructionSite() const
{
    CheckGameThread("GetConstructionSite");
    return ActiveConstructionSite;
}

bool FObjectRegistry::IsClassDefaultObject(uint64_t UniqueID) const
{
    CheckGameThread("IsClassDefaultObject");
    const FLiveInstanceRecord* Found = LiveInstances.Find(UniqueID);
    return Found && Found->Kind == EInstanceKind::ClassDefaultObject;
}

// ==============================================================================
// Module lifetime scopes
// ==============================================================================

void FObjectRegistry::EnsureBootstrapClasses()
{
    if (bBootstrapClassesRegistered)
    {
        return;
    }

    // Set before registering: RegisterClass constructs a default object, and that constructor
    // may re-enter the registry.
    bBootstrapClassesRegistered = true;

    // RObject first: FObjectScope's superclass has to resolve.
    if (!RegisterClass<RObject>(BootstrapModuleName))
    {
        VENGINE_LOG(LogCore, Fatal, "[ObjectRegistry] Failed to register the bootstrap class 'RObject'.");
        return;
    }
    if (!RegisterClass<FObjectScope>(BootstrapModuleName))
    {
        VENGINE_LOG(LogCore, Fatal, "[ObjectRegistry] Failed to register the bootstrap class '{}'.", FObjectScope::StaticClassName());
        return;
    }
}

void FObjectRegistry::Shutdown()
{
    CheckGameThread("Shutdown");

    // Module teardown must already have run (FModuleManager::UnloadAllModules). The registry does
    // not know about modules, so it does not touch scopes here: a surviving scope IS the report.
    AuditLiveObjects("engine shutdown");

    // Class default objects are metadata, not gameplay objects. Destroy them last, repeatedly,
    // because destroying one converts its default subobjects into ordinary instances.
    bool bDestroyedAny = true;
    while (bDestroyedAny)
    {
        bDestroyedAny = false;
        for (auto& Pair : Classes)
        {
            if (Pair.second.ClassDefaultObject)
            {
                DestroyClassDefaultObject(Pair.second);
                bDestroyedAny = true;
                break;
            }
        }
    }
}

RObject* FObjectRegistry::CreateScope(const FString& ScopeName)
{
    CheckGameThread("CreateScope");
    EnsureBootstrapClasses();

    FClassInfo* ScopeClass = Classes.Find(FObjectScope::StaticClassName());
    if (!ScopeClass || !ScopeClass->Factory)
    {
        // Unreachable: EnsureBootstrapClasses() registers it or aborts. Kept as a backstop.
        VENGINE_LOG(LogCore, Fatal, "[ObjectRegistry] '{}' is not registered; cannot create a lifetime scope.", FObjectScope::StaticClassName());
        return nullptr;
    }

    // Constructed with no outer on purpose: the scope is the root of a module's subtree, owned by
    // whoever called us (FModuleManager), not by another object. This is the only path in the
    // engine that bypasses the "every object has an outer" rule.
    FObjectScope* Scope = static_cast<FObjectScope*>(ConstructInternal(ScopeClass, nullptr));
    if (!Scope)
    {
        VENGINE_LOG(LogCore, Fatal, "[ObjectRegistry] Failed to construct the lifetime scope '{}'.", ScopeName.CStr());
        return nullptr;
    }

    Scope->ScopeName = ScopeName;
    VENGINE_LOG(LogCore, Verbose, "[ObjectRegistry] Created lifetime scope '{}' (ID: {})", ScopeName.CStr(), Scope->GetUniqueID());
    return Scope;
}

void FObjectRegistry::DestroyScope(RObject* Scope)
{
    CheckGameThread("DestroyScope");
    if (!Scope)
    {
        return;
    }

    const FClassInfo* ScopeClass = Classes.Find(FObjectScope::StaticClassName());
    if (!ScopeClass || !Scope->IsA(ScopeClass))
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ObjectRegistry] DestroyScope() was given an object of class '{}', which is not a lifetime scope.",
            Scope->GetClassName()
        );
        return;
    }

    const int32_t Children = Scope->GetChildren().Num();
    if (Children != 0)
    {
        const FObjectScope* Named = static_cast<const FObjectScope*>(Scope);
        VENGINE_LOG(LogCore, Verbose, "[ObjectRegistry] Lifetime scope '{}' is taking {} object(s) with it.", Named->ScopeName.CStr(), Children);
    }

    delete Scope;   // cascades through DestroyChildren()
}

int32_t FObjectRegistry::AuditLiveObjects(const char* Phase) const
{
    CheckGameThread("AuditLiveObjects");

    const FClassInfo* ScopeClass = Classes.Find(FObjectScope::StaticClassName());

    int32_t Survivors = 0;
    for (const auto& Pair : LiveInstances)
    {
        const FLiveInstanceRecord& Record = Pair.second;

        // Class default objects are metadata, and default subobjects are owned by one.
        if (Record.Kind != EInstanceKind::Ordinary)
        {
            continue;
        }

        // A surviving scope means a module was never unloaded. Report it as the leak it is:
        // everything it owns is still alive too.
        if (Record.ClassInfo == ScopeClass)
        {
            const FObjectScope* Scope = static_cast<const FObjectScope*>(Record.Instance);
            VENGINE_LOG(
                LogCore,
                Error,
                "[ObjectRegistry] Leaked lifetime scope at {}: '{}' (ID: {}) still owns {} object(s). Its module was never unloaded.",
                Phase,
                Scope->ScopeName.CStr(),
                Pair.first,
                Scope->GetChildren().Num()
            );
            ++Survivors;
            continue;
        }

        const RObject* Outer = Record.Instance->GetOuter();
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] Leaked object at {}: '{}' (ID: {}, outer: '{}/{}'). Nothing owns it, so it lives until process exit.",
            Phase,
            Record.ClassInfo ? Record.ClassInfo->Name.CStr() : "<unknown>",
            Pair.first,
            Outer ? Outer->GetClassName() : "None",
            Outer ? Outer->GetUniqueID() : 0
        );
        ++Survivors;
    }

    if (Survivors != 0)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRegistry] {} object(s) leaked at {}. Objects that cross a module boundary need a TWeakObjectPtr, not a raw pointer.",
            Survivors,
            Phase
        );
    }
    return Survivors;
}

// Only the registry may delete an RObject: operator delete is protected and Destroy() is the
// public path. Class default objects are owned by the registry and cannot be destroyed here.
void FObjectRegistry::DestroyObject(RObject* Obj)
{
    if (!Obj)
    {
        return;
    }

    CheckGameThread("Destroy");

    if (IsClassDefaultObject(Obj->GetUniqueID()))
    {
        VENGINE_LOG(LogCore, Error, "[RObject] Refusing to Destroy() the class default object of '{}'. It is owned by the registry.", Obj->GetClassName());
        return;
    }

    delete Obj;
}

// ==============================================================================
// RObject Ownership Tree Implementation
// ==============================================================================

RObject::RObject(RObject* InOuter)
{
    FObjectRegistry& Registry = FObjectRegistry::Get();
    Registry.CheckGameThread("RObject::RObject");

    UniqueID = AllocateUniqueID();
    Registry.RegisterInstance(UniqueID, this);   // also assigns ClassInfo

    if (InOuter)
    {
        SetOuter(InOuter);
    }
}

RObject::~RObject()
{
    FObjectRegistry::Get().CheckGameThread("~RObject");

    FObjectRegistry::Get().UnregisterInstance(UniqueID);

    DestroyChildren();

    if (Outer)
    {
        Outer->RemoveChild(this);
        Outer = nullptr;
    }
}

void RObject::SetOuter(RObject* InOuter)
{
    FObjectRegistry::Get().CheckGameThread("SetOuter");

    if (Outer == InOuter)
    {
        return;
    }

    for (const RObject* Ancestor = InOuter; Ancestor != nullptr; Ancestor = Ancestor->GetOuter())
    {
        if (Ancestor == this)
        {
            VENGINE_LOG(LogCore, Warning, "[RObject] Rejected cyclic SetOuter() on '{}' (ID: {})", GetClassName(), UniqueID);
            return;
        }
    }

    if (Outer)
    {
        Outer->RemoveChild(this);
    }
    Outer = InOuter;
    if (Outer)
    {
        Outer->AddChild(this);
    }
}

void RObject::AddChild(RObject* Child)
{
    if (Child)
    {
        OwnedChildren.Add(Child);
    }
}

void RObject::RemoveChild(RObject* Child)
{
    OwnedChildren.Remove(Child);
}

void RObject::DestroyChildren()
{
    while (!OwnedChildren.IsEmpty())
    {
        RObject* Child = OwnedChildren[OwnedChildren.Num() - 1];
        delete Child;   // ~RObject removes itself from OwnedChildren
    }
}