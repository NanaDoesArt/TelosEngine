#pragma once
#include "CoreAPI.h"
#include "Object/Interface.h"
#include "Containers/String.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include <cstdint>
#include <type_traits>

#include "Templates/Function.h"

class RObject;
class FModuleManager;

// Result of a class registration.
//
// Registered and AlreadyRegistered are both success: a class declared in a header is
// compiled into every module that includes it, so the same registration legitimately
// arrives more than once and the extra copies must be accepted and ignored. The
// distinction matters for one thing only: interfaces are attached to the class exactly
// once, by the call that actually created the metadata.
enum class EClassRegistrationResult : uint8_t
{
    Registered,
    AlreadyRegistered,
    Failed
};

struct FClassInfo
{
    FString Name;
    FString SuperClassName;
    FString OwningModule;
    const FClassInfo* SuperClass = nullptr;
    RObject* ClassDefaultObject = nullptr;
    TFunction<RObject*()> Factory;
    TFunction<void(RObject*)> CdoDeleter;

    // Interfaces the class declared with RIMPLEMENTS(). Empty for most classes.
    // Appended exactly once, before the default object is constructed.
    TArray<FImplementedInterface> Interfaces;

    // Objects of this class created outside of a constructor (NewObject<T>() outside any
    // construction). These always block the class (and its module) from being unregistered.
    int32_t LiveInstanceCount = 0;

    // Objects of this class created while some class's default object (CDO) was being constructed.
    // They are owned by that CDO and are destroyed with it. Until then they keep this class alive.
    int32_t DefaultSubobjectCount = 0;

    bool IsChildOf(const FClassInfo* Other) const
    {
        if (!Other)
        {
            return false;
        }
        const FClassInfo* Current = this;
        while (Current)
        {
            if (Current == Other)
            {
                return true;
            }
            Current = Current->SuperClass;
        }
        return false;
    }

    // Looks in this class first, then in its super classes. The walk is what makes a
    // subclass inherit its parent's interfaces without re-listing them: the parent's
    // thunk casts RObject* to the parent, which is a plain upcast for a child instance.
    const FImplementedInterface* FindInterface(const char* InterfaceName) const
    {
        for (const FClassInfo* Current = this; Current; Current = Current->SuperClass)
        {
            for (const FImplementedInterface& Entry : Current->Interfaces)
            {
                if (Entry.Name == InterfaceName)
                {
                    return &Entry;
                }
            }
        }
        return nullptr;
    }

    // nullptr when the class (and its bases) does not implement the interface.
    void* GetInterfaceAddress(RObject* Object, const char* InterfaceName) const
    {
        const FImplementedInterface* Entry = FindInterface(InterfaceName);
        return (Entry && Entry->GetAddress) ? Entry->GetAddress(Object) : nullptr;
    }
};

// Appends one interface to a class's list, ignoring a duplicate name (interface diamonds).
//
// Defined in Core on purpose. The name is stored in an FString, and the buffer must be
// allocated and freed by the same module. If this were inline, the FString would be built
// in the module that registers the class and freed in Core when the class is removed, which
// is two different allocators. Here the FString is built and freed by Core in both cases.
Core_API void AddClassInterface(FClassInfo& Info, const char* InterfaceName, void* (*GetAddress)(RObject*));

namespace InterfaceRegistrationDetail
{
    // Object* -> TInterface*. Only the class's own translation unit can compute this offset.
    // The registry calls it for objects whose exact class is TClass or a subclass of it.
    template <typename TClass, typename TInterface>
    void* TInterfaceAddressThunk(RObject* Object)
    {
        return static_cast<TInterface*>(static_cast<TClass*>(Object));
    }

    template <typename TClass, typename TInterface>
    void TAddInterfaceOfType(FClassInfo& Info);

    // Expands an interface's InterfaceBases list. A non-template struct specialised on
    // TInterfaceList is the C++20 way to iterate a parameter pack without a helper lambda.
    template <typename TClass, typename TList>
    struct TInterfaceBasesAttacher;

    template <typename TClass, typename... TInterfaces>
    struct TInterfaceBasesAttacher<TClass, TInterfaceList<TInterfaces...>>
    {
        static void Attach(FClassInfo& Info)
        {
            (TAddInterfaceOfType<TClass, TInterfaces>(Info), ...);
        }
    };

    template <typename TClass, typename TInterface>
    void TAddInterfaceOfType(FClassInfo& Info)
    {
        static_assert(
            std::is_base_of_v<TInterface, TClass>,
            "RIMPLEMENTS()/RINTERFACE() lists an interface that this class does not inherit."
        );

        // Catches private and ambiguous inheritance with a readable error instead of a
        // pointer pointing at the wrong subobject.
        static_assert(
            std::is_convertible_v<TClass*, TInterface*>,
            "Interface base is private or ambiguous. Inherit it publicly and unambiguously, "
            "e.g. 'class WFoo : public RObject, public IFoo'."
        );

        AddClassInterface(Info, TInterface::StaticInterfaceName(), &TInterfaceAddressThunk<TClass, TInterface>);

        // Extending an interface is a promise to implement its bases too.
        if constexpr (requires { typename TInterface::InterfaceBases; })
        {
            TInterfaceBasesAttacher<TClass, typename TInterface::InterfaceBases>::Attach(Info);
        }
    }

    template <typename TClass>
    void TAttachClassInterfaces(FClassInfo& Info)
    {
        TInterfaceBasesAttacher<TClass, typename TClass::InterfaceTypes>::Attach(Info);
    }
}

class Core_API FObjectRegistry
{
public:
    static FObjectRegistry& Get();

    void BindGameThread();
    bool IsOnGameThread() const;
    void CheckGameThread(const char* Operation) const;

    // The first module to register a class owns it, and the first module to register is always
    // the one that declares the class: including a header requires depending on the module that
    // owns it, and dependencies boot first.
    //
    // A repeated registration with the SAME base class is accepted and ignored: the same class
    // declared in a shared header is compiled into every module that includes it, so it
    // legitimately arrives more than once. A repeated name with a DIFFERENT base class is a real
    // collision between two classes and is rejected.
    //
    // AttachInterfaces is called once, after the metadata exists and BEFORE the default object
    // is constructed, so a default object constructor can Cast<Interface>(this) like any other
    // object. It is null for classes that declare no interfaces.
    //
    // Returns Failed only for a rejected registration (empty factory, or a name collision).
    // Rethrows if the default object constructor throws, after rolling back.
    EClassRegistrationResult RegisterClass(
        const FString& Name,
        const FString& SuperName,
        const FString& ModuleName,
        TFunction<RObject*()> Factory,
        TFunction<void(RObject*)> CdoDeleter,
        void (*AttachInterfaces)(FClassInfo&) = nullptr
    );

    // Destroys the class's default object first, then removes the metadata, but only if this
    // module owns the class. Every other module's copy of the registrar is a no-op here.
    // Fatal if ordinary instances exist, if a subclass still has objects, or if default objects
    // of OTHER modules still hold objects of this class.
    void UnregisterClass(const FString& Name, const FString& ModuleName);

    // ModuleName is required on purpose: ownership decides which module may unregister the
    // class, and a default like "Unknown" would make that decision wrong.
    template <typename TClass>
    EClassRegistrationResult RegisterClass(const FString& ModuleName)
    {
        FString SuperName = "";
        if constexpr (!std::is_same_v<TClass, RObject>)
        {
            SuperName = TClass::Super::StaticClassName();
        }

        void (*AttachInterfaces)(FClassInfo&) = nullptr;
        if constexpr (requires { typename TClass::InterfaceTypes; })
        {
            AttachInterfaces = &InterfaceRegistrationDetail::TAttachClassInterfaces<TClass>;
        }

        return RegisterClass(
            TClass::StaticClassName(),
            SuperName,
            ModuleName,
            []() -> RObject* { return new TClass(); },
            [](RObject* Ptr) { delete static_cast<TClass*>(Ptr); },
            AttachInterfaces
        );
    }

    template <typename TClass>
    void UnregisterClass(const FString& ModuleName)
    {
        UnregisterClass(TClass::StaticClassName(), ModuleName);
    }

    // Typed construction: rejects a null outer, nullptr RequiredBaseClass or incompatible classes
    // BEFORE constructing. Every object must be owned by something; NewObject<T>() supplies an
    // owner from context, and the registry refuses construction without one.
    RObject* ConstructObjectOfType(const FString& Name, RObject* InOuter, const FClassInfo* RequiredBaseClass);

    // Explicit unrestricted construction by class name (returns base RObject*). Also requires an outer.
    RObject* ConstructObjectByName(const FString& Name, RObject* InOuter);

    const RObject* GetCDO(const FString& Name) const;
    const FClassInfo* FindClass(const FString& Name) const;

    // True while code of this module is still in use by something the module will not clean up by itself:
    //  - ordinary instances of the module's classes, and
    //  - default subobjects of the module's classes that sit inside default objects of OTHER modules.
    // Default subobjects owned by the module's own default objects do not count: they die when the module unloads.
    bool HasLiveInstancesForModule(const FString& ModuleName, int32_t* OutCount = nullptr) const;

    // Live instance tracking (stores exact FClassInfo* at construction so ~RObject never relies on virtual calls)
    void RegisterInstance(uint64_t UniqueID, RObject* Instance);
    void UnregisterInstance(uint64_t UniqueID);
    RObject* FindInstance(uint64_t UniqueID) const;

    // Non-null while an object's constructor is running: the object currently being built.
    // NewObject<T>() uses it to decide ownership, which is what makes default subobjects work
    // without a separate creation function. Engine internals; call sites use NewObject<T>().
    RObject* GetConstructionSite() const;

    // O(1) counters for the console dashboard and diagnostics. Class default objects are metadata,
    // and every successfully registered class has exactly one (a class whose default object failed
    // to build is rolled back with it), so they cancel out here. Never use these to decide whether
    // a module can unload: that is HasLiveInstancesForModule().
    int32_t GetLiveObjectCount() const
    {
        CheckGameThread("GetLiveObjectCount");
        return LiveInstances.Num() - Classes.Num();
    }

    int32_t GetRegisteredClassCount() const
    {
        CheckGameThread("GetRegisteredClassCount");
        return Classes.Num();
    }

    // Deletes an ordinary object. Refuses class default objects. This is the implementation of
    // Destroy(); the registry is the only thing allowed to call delete on an RObject.
    void DestroyObject(RObject* Obj);

    // Class default objects are owned by their FClassInfo and must never be destroyed by user code
    bool IsClassDefaultObject(uint64_t UniqueID) const;

    // Logs every ordinary object that is still alive and returns how many were found.
    // Call at shutdown to prove nothing was orphaned.
    int32_t AuditLiveObjects(const char* Phase) const;

    // Audits survivors, then destroys every class default object. Call once from the engine loop's
    // Exit(), AFTER FModuleManager::UnloadAllModules(). Module authors never call this.
    void Shutdown();

private:
    // ==========================================================================
    // Lifetime roots. Mechanism only.
    //
    // The registry can create and destroy a root, but deliberately does NOT remember which
    // root belongs to which module, or when it should die: that is module policy and lives in
    // FModuleManager. Keeping the name -> root map there means there is exactly one list of
    // modules in the process.
    // ==========================================================================
    friend class FModuleManager;

    // Creates an FObjectScope with no outer. The ONLY object in the engine allowed to have no
    // owner, and not reachable from game code. Caller becomes responsible for DestroyScope().
    RObject* CreateScope(const FString& ScopeName);

    // Destroys a scope and its whole subtree. Fatal if handed something that is not a scope.
    void DestroyScope(RObject* Scope);

    enum class EInstanceKind : uint8_t
    {
        Ordinary,           // created outside a constructor, counted in FClassInfo::LiveInstanceCount
        DefaultSubobject,   // created inside a CDO constructor, counted in FClassInfo::DefaultSubobjectCount
        ClassDefaultObject  // the CDO itself, owned by FClassInfo, not counted
    };

    struct FLiveInstanceRecord
    {
        RObject* Instance = nullptr;
        FClassInfo* ClassInfo = nullptr;    // exact class of the object, always set
        FClassInfo* OwnerClass = nullptr;   // DefaultSubobject only: class whose CDO owns this object
        EInstanceKind Kind = EInstanceKind::Ordinary;
    };

    // Bookkeeping for one class default object construction (defined in Object.cpp)
    struct FCdoBuildContext;

    void ResolveSuperClasses();
    RObject* ConstructInternal(FClassInfo* Info, RObject* InOuter);
    void DestroyClassDefaultObject(FClassInfo& Info);
    int32_t DestroyUnownedDefaultSubobjects(FCdoBuildContext& Context, const RObject* Root);

    // Registers the engine's own bootstrap classes (RObject, FObjectScope) on first use.
    // Nobody registers these by hand. FObjectManager-independent: everything that needs a
    // FClassInfo goes through the registry first, so ordering is guaranteed.
    void EnsureBootstrapClasses();

    // Classes owned by this name can never match a real module, so bootstrap classes never
    // block a module from unloading and never appear in leak audits.
    static constexpr const char* BootstrapModuleName = "VEngine.Bootstrap";

    TMap<FString, FClassInfo> Classes;
    TMap<uint64_t, FLiveInstanceRecord> LiveInstances;
    FClassInfo* ActiveConstructingClass = nullptr;
    RObject* ActiveConstructionSite = nullptr;
    FCdoBuildContext* ActiveCdoContext = nullptr;
    bool bBootstrapClassesRegistered = false;
};