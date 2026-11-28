#include "Reflection/ReflectionRegistry.h"
#include "Containers/Map.h"
#include "Logging/Log.h"
#include "Modules/ModuleInterface.h"
#include <cstring>
#include <utility>

namespace
{
    // The built-in descriptors are instantiated in this module, so their operation pointers
    // belong to this module's library and this module must release them before it is freed.
    // The module name comes from the build definition rather than a literal so a renamed
    // module folder does not silently leave orphaned metadata behind.
    constexpr const char* BuiltinModuleName = VENGINE_CURRENT_MODULE_NAME;

    // ==========================================================================
    // Process-wide storage
    //
    // Kept in this translation unit so the public header stays a pure
    // declaration and the table never crosses the module boundary. TMap is an
    // unordered_map underneath: it is node based, so a pointer to a value stays
    // valid when other types are inserted or erased. That is what makes
    // FPropertyInfo::Type and every FindType() result safe to hold until the type
    // it points at is unregistered.
    // ==========================================================================
    struct FReflectionStorage
    {
        TMap<FString, FTypeInfo> Types;
    };

    FReflectionStorage& Storage()
    {
        static FReflectionStorage Instance;
        return Instance;
    }

    const char* DescribeKind(ETypeKind Kind)
    {
        switch (Kind)
        {
        case ETypeKind::Primitive:
            return "Primitive";
        case ETypeKind::Enum:
            return "Enum";
        case ETypeKind::Struct:
            return "Struct";
        case ETypeKind::Class:
            return "Class";
        }
        return "Unknown";
    }

    // The identity a property uses to name its member type. A descriptor built from
    // TMemberProperty always carries TypeName; a hand-built one may instead point Type
    // at metadata directly. Both spell the same thing, so compare the resolved name
    // when there is no interned identity.
    const char* TypeIdentity(const FPropertyInfo& Property)
    {
        if (Property.TypeName)
        {
            return Property.TypeName;
        }
        return Property.Type ? Property.Type->Name.CStr() : "";
    }

    bool PropertyRefersTo(const FPropertyInfo& Property, const FTypeInfo& Type)
    {
        if (Property.Type == &Type)
        {
            return true;
        }
        if (Property.TypeName)
        {
            return std::strcmp(Property.TypeName, Type.Name.CStr()) == 0;
        }
        return false;
    }

    // Two registrations of one name describe the same type only when they agree on
    // everything a consumer can observe without running code. Function pointers are
    // deliberately not compared: every module instantiates its own copy of the same
    // template, so the addresses differ even when the code is identical. The first
    // registration's pointers are kept.
    bool DescriptorsAgree(const FTypeInfo& Existing, const FTypeInfo& Incoming)
    {
        const char* Name = Existing.Name.CStr();

        if (Existing.Kind != Incoming.Kind)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ReflectionRegistry] Kind mismatch for '{}': '{}' registered it as {}, '{}' as {}. "
                "Rebuild every module against the same headers.",
                Name,
                Existing.OwningModule.CStr(),
                DescribeKind(Existing.Kind),
                Incoming.OwningModule.CStr(),
                DescribeKind(Incoming.Kind)
            );
            return false;
        }

        if (Existing.Size != Incoming.Size || Existing.Alignment != Incoming.Alignment)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ReflectionRegistry] Layout mismatch for '{}': '{}' registered sizeof={} align={}, "
                "'{}' compiled sizeof={} align={}. Rebuild every module against the same headers.",
                Name,
                Existing.OwningModule.CStr(),
                Existing.Size,
                Existing.Alignment,
                Incoming.OwningModule.CStr(),
                Incoming.Size,
                Incoming.Alignment
            );
            return false;
        }

        if (Existing.Properties.Num() != Incoming.Properties.Num())
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ReflectionRegistry] Property count mismatch for '{}': '{}' registered {}, '{}' registered {}. "
                "Rebuild every module against the same headers.",
                Name,
                Existing.OwningModule.CStr(),
                Existing.Properties.Num(),
                Incoming.OwningModule.CStr(),
                Incoming.Properties.Num()
            );
            return false;
        }

        for (int32_t Index = 0; Index < Existing.Properties.Num(); ++Index)
        {
            const FPropertyInfo& Registered = Existing.Properties[Index];
            const FPropertyInfo& Candidate = Incoming.Properties[Index];

            if (Registered.Name != Candidate.Name)
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ReflectionRegistry] Property mismatch for '{}' at index {}: '{}' registered '{}', '{}' registered '{}'. "
                    "Property order is part of the descriptor. Rebuild every module against the same headers.",
                    Name,
                    Index,
                    Existing.OwningModule.CStr(),
                    Registered.Name.CStr(),
                    Incoming.OwningModule.CStr(),
                    Candidate.Name.CStr()
                );
                return false;
            }

            if (std::strcmp(TypeIdentity(Registered), TypeIdentity(Candidate)) != 0)
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ReflectionRegistry] Property '{}' of '{}' has type '{}' according to '{}', but '{}' according to '{}'. "
                    "Rebuild every module against the same headers.",
                    Registered.Name.CStr(),
                    Name,
                    TypeIdentity(Registered),
                    Existing.OwningModule.CStr(),
                    TypeIdentity(Candidate),
                    Incoming.OwningModule.CStr()
                );
                return false;
            }

            if (Registered.IsReadOnly() != Candidate.IsReadOnly())
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ReflectionRegistry] Property '{}' of '{}' is {} according to '{}' and {} according to '{}'. "
                    "Rebuild every module against the same headers.",
                    Registered.Name.CStr(),
                    Name,
                    Registered.IsReadOnly() ? "read-only" : "mutable",
                    Existing.OwningModule.CStr(),
                    Candidate.IsReadOnly() ? "read-only" : "mutable",
                    Incoming.OwningModule.CStr()
                );
                return false;
            }
        }

        return true;
    }

    // ==========================================================================
    // Built-in value types
    //
    // Values whose layout and semantics the compiler already knows: no reflected
    // members, but a real size, and whatever copy/compare operations C++ provides.
    // Registering them once here means every module's property list resolves a
    // 'float' or an 'FString' member instead of each module registering the same
    // primitives again.
    //
    // Aliases are absent on purpose: int32_t is int on this platform and
    // QualifiedTypeName<int32_t>() already returns "int", so listing both would
    // only produce a duplicate registration of the same name.
    //
    // FString is a value type with no object-runtime semantics. It is described as
    // a Struct with no properties: the registry knows its size, can construct,
    // copy, destroy and compare it, but does not pretend to model its internals.
    // ==========================================================================
    void EnsureBuiltinTypes(FReflectionRegistry& Registry)
    {
        // Set before registering. RegisterType(FTypeInfo) calls back into this function,
        // and a second pass over the same list would recurse without making progress.
        static bool bRegistered = false;
        if (bRegistered)
        {
            return;
        }
        bRegistered = true;

        Registry.RegisterType<bool>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<char>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<signed char>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<unsigned char>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<wchar_t>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<char8_t>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<char16_t>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<char32_t>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<short>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<unsigned short>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<int>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<unsigned int>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<long>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<unsigned long>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<long long>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<unsigned long long>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<float>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<double>(BuiltinModuleName, ETypeKind::Primitive);
        Registry.RegisterType<long double>(BuiltinModuleName, ETypeKind::Primitive);

        Registry.RegisterType<FString>(BuiltinModuleName, ETypeKind::Struct);
    }
}

FReflectionRegistry& FReflectionRegistry::Get()
{
    static FReflectionRegistry Instance;

    // Installed on first access rather than during static initialization: module code may
    // register a type before main() runs, and the registry has to be complete by then.
    EnsureBuiltinTypes(Instance);
    return Instance;
}

ETypeRegistrationResult FReflectionRegistry::RegisterType(FTypeInfo Info)
{
    EnsureBuiltinTypes(*this);

    if (Info.Name.IsEmpty())
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ReflectionRegistry] Refusing to register a type with no name (module '{}').",
            Info.OwningModule.CStr()
        );
        return ETypeRegistrationResult::Failed;
    }

    if (Info.OwningModule.IsEmpty())
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ReflectionRegistry] Refusing to register '{}': it has no owning module, so nothing could ever unregister it.",
            Info.Name.CStr()
        );
        return ETypeRegistrationResult::Failed;
    }

    if (Info.Size == 0 || Info.Alignment == 0)
    {
        // sizeof is never zero for a complete object type. A descriptor that says
        // otherwise was not built by a type-aware registration.
        VENGINE_LOG(
            LogCore,
            Error,
            "[ReflectionRegistry] Refusing to register '{}': it describes no storage (sizeof={}, alignof={}).",
            Info.Name.CStr(),
            Info.Size,
            Info.Alignment
        );
        return ETypeRegistrationResult::Failed;
    }

    if (const FTypeInfo* Existing = Storage().Types.Find(Info.Name))
    {
        if (!DescriptorsAgree(*Existing, Info))
        {
            return ETypeRegistrationResult::Failed;
        }

        // A header-defined type is compiled into every module that includes it, so a
        // second descriptor is expected. The first registrant owns the metadata; its
        // module boots first because including the header requires depending on it.
        VENGINE_LOG(
            LogCore,
            Verbose,
            "[ReflectionRegistry] '{}' already registered by '{}'; ignoring the duplicate from '{}'.",
            Info.Name.CStr(),
            Existing->OwningModule.CStr(),
            Info.OwningModule.CStr()
        );
        return ETypeRegistrationResult::AlreadyRegistered;
    }

    // Keep the name for the log line: the descriptor is moved into the table below.
    const FString RegisteredName = Info.Name;

    FTypeInfo& Registered = Storage().Types.FindOrAdd(Info.Name);
    Registered = std::move(Info);

    // Member types may have been registered after the type that names them, so resolve
    // every property that is still waiting, not just the new type's. Registration is a
    // module-load-time operation; the quadratic scan is not on a hot path.
    ResolveProperties();
    
    VENGINE_LOG(
        LogCore,
        Verbose,
        "[ReflectionRegistry] Registered type '{}' (module '{}').",
        RegisteredName.CStr(),
        Registered.OwningModule.CStr()
    );
    return ETypeRegistrationResult::Registered;
}

const FTypeInfo* FReflectionRegistry::FindType(const FString& Name) const
{
    return Storage().Types.Find(Name);
}

const FTypeInfo* FReflectionRegistry::FindType(const char* Name) const
{
    if (!Name || !*Name)
    {
        return nullptr;
    }
    return FindType(FString(Name));
}

bool FReflectionRegistry::UnregisterType(const FString& Name, const FString& ModuleName)
{
    if (Name.IsEmpty())
    {
        return true;
    }

    FTypeInfo* Existing = Storage().Types.Find(Name);
    if (!Existing)
    {
        // Already removed. A finalization pass that is retried must report success.
        return true;
    }

    // Another module's copy of the same header-defined descriptor is a no-op: removing
    // metadata the owner still relies on would delete it out from under that module.
    if (Existing->OwningModule != ModuleName)
    {
        return true;
    }

    // A live property holds FTypeInfo* for its member type. Removing this type now would
    // leave that pointer dangling while the referring type is still findable. Refuse and
    // let the caller retry after the referring type is gone. Same-module referrers are
    // included: module unregistration retries until a pass makes no progress.
    for (const auto& Pair : Storage().Types)
    {
        const FTypeInfo& Other = Pair.second;
        if (&Other == Existing)
        {
            continue;
        }

        for (const FPropertyInfo& Property : Other.Properties)
        {
            if (PropertyRefersTo(Property, *Existing))
            {
                VENGINE_LOG(
                    LogCore,
                    Warning,
                    "[ReflectionRegistry] Delaying unregistration of type '{}': property '{}' of '{}' (module '{}') still refers to it.",
                    Name.CStr(),
                    Property.Name.CStr(),
                    Other.Name.CStr(),
                    Other.OwningModule.CStr()
                );
                return false;
            }
        }
    }

    Storage().Types.Remove(Name);
    VENGINE_LOG(
        LogCore,
        Verbose,
        "[ReflectionRegistry] Unregistered type '{}' (module '{}').",
        Name.CStr(),
        ModuleName.CStr()
    );
    return true;
}

bool FReflectionRegistry::UnregisterType(const char* Name, const FString& ModuleName)
{
    if (!Name || !*Name)
    {
        return true;
    }
    return UnregisterType(FString(Name), ModuleName);
}

bool FReflectionRegistry::UnregisterModuleTypes(const FString& ModuleName)
{
    bool bRemovedAny = true;
    while (bRemovedAny)
    {
        bRemovedAny = false;

        // Snapshot the names first: erasing a type invalidates the iterator that found it,
        // and UnregisterType() itself may refuse while a referrer is still registered.
        TArray<FString> OwnedNames;
        for (const auto& Pair : Storage().Types)
        {
            if (Pair.second.OwningModule == ModuleName)
            {
                OwnedNames.Add(Pair.first);
            }
        }

        for (const FString& Name : OwnedNames)
        {
            if (!Storage().Types.Contains(Name))
            {
                continue;
            }

            // UnregisterType() also reports success for a type that is already gone, so
            // confirm that the entry really disappeared before claiming progress.
            if (UnregisterType(Name, ModuleName) && !Storage().Types.Contains(Name))
            {
                bRemovedAny = true;
            }
        }
    }

    int32_t Remaining = 0;
    for (const auto& Pair : Storage().Types)
    {
        if (Pair.second.OwningModule == ModuleName)
        {
            VENGINE_LOG(
                LogCore,
                Warning,
                "[ReflectionRegistry] Type '{}' of module '{}' is still registered: a property of a registered type refers to it. "
                "Unregister the referring type first.",
                Pair.first.CStr(),
                ModuleName.CStr()
            );
            ++Remaining;
        }
    }

    return Remaining == 0;
}

void FReflectionRegistry::ResolveProperties()
{
    for (auto& Pair : Storage().Types)
    {
        FTypeInfo& Type = Pair.second;
        for (FPropertyInfo& Property : Type.Properties)
        {
            if (Property.Type || !Property.TypeName)
            {
                continue;
            }

            Property.Type = Storage().Types.Find(FString(Property.TypeName));
        }
    }
}

int32_t FReflectionRegistry::CountUnresolvedProperties() const
{
    int32_t Count = 0;
    for (const auto& Pair : Storage().Types)
    {
        for (const FPropertyInfo& Property : Pair.second.Properties)
        {
            if (!Property.Type)
            {
                ++Count;
            }
        }
    }
    return Count;
}

int32_t FReflectionRegistry::GetRegisteredTypeCount() const
{
    return Storage().Types.Num();
}