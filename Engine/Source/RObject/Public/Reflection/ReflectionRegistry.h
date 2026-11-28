#pragma once
#include "RObjectAPI.h"
#include "Containers/String.h"
#include "Reflection/TypeInfo.h"
#include "Templates/TypeName.h"
#include <cstdint>
#include <type_traits>

// Result of a type registration.
//
// Registered and AlreadyRegistered are both success. A type declared in a header is
// compiled into every module that includes it, so the same descriptor legitimately
// arrives more than once; the extra copies are accepted and ignored. Only the first
// registration owns the metadata and its function pointers.
//
// Failed means two different views of one type name disagree: a different kind, a
// different layout, or a different property list. That is an ODR-level mistake and
// must be reported instead of silently keeping one module's definition for both.
enum class ETypeRegistrationResult : uint8_t
{
    Registered,
    AlreadyRegistered,
    Failed
};

namespace ReflectionRegistryDetail
{
    // Only ordinary object types are values the registry can describe. Arrays need an
    // element/count descriptor that does not exist yet, and TTypeOperationsFor cannot
    // copy or compare an array without decaying it to a pointer. A member of array type
    // may still appear in a property list; its Type stays unresolved until array support
    // arrives, which is visible through CountUnresolvedProperties().
    template <typename T>
    constexpr void ValidateValueType()
    {
        static_assert(!std::is_void_v<T>, "A reflected value type cannot be void.");
        static_assert(!std::is_reference_v<T>, "Register the referred-to type, not a reference.");
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
            "Register the unqualified value type, not a cv-qualified one.");
        static_assert(!std::is_function_v<T>, "A reflected value type cannot be a function type.");
        static_assert(!std::is_array_v<T>,
            "Array types are not reflected yet. Reflect the element type; an array member "
            "keeps its TypeName and stays unresolved until array support exists.");
        static_assert(!std::is_union_v<T>, "Union reflection is not supported yet.");
        static_assert(std::is_object_v<T>, "A reflected value type must be an object type.");
    }
}

// ==============================================================================
// Reflection registry
//
// The one owner and lookup point for FTypeInfo metadata. It registers types, resolves
// member-type identities, handles duplicate registrations, records which module owns
// each type, and removes metadata before its owning library is freed.
//
// It does not create or destroy values, construct CDOs, or own objects. Those remain
// the object runtime's responsibilities.
//
// Threading: registration and lookup happen on the game thread during module load,
// module teardown, and engine startup. The registry is not internally synchronized.
//
// Lifetime: FindType() returns a pointer into registry-owned storage. The pointer is
// valid until that type is unregistered. A type cannot be unregistered while another
// registered type has a property that names it, so a property's Type pointer does not
// dangle when its module unloads first. Consumers that cache a pointer across a module
// unload must re-query it afterwards.
// ==============================================================================

class RObject_API FReflectionRegistry
{
public:
    static FReflectionRegistry& Get();

    // Low-level registration. The caller has already described the type, including its
    // property list. The registry validates the descriptor, takes ownership of its
    // strings and property array, registers the type shell immediately (so later types
    // can refer to it), and resolves any properties whose member types are now known.
    //
    // Taking FTypeInfo by value lets a builder hand over a finished descriptor without
    // a second copy; the registry moves it into its table.
    ETypeRegistrationResult RegisterType(FTypeInfo Info);

    // Describes T from its compiler facts and registers it with no properties. Suitable
    // for built-in values and for types whose members are registered separately. A type
    // with reflected members uses the property builder instead, which fills Properties
    // before calling RegisterType(FTypeInfo).
    template <typename T>
    ETypeRegistrationResult RegisterType(const FString& ModuleName, ETypeKind Kind)
    {
        ReflectionRegistryDetail::ValidateValueType<T>();

        FTypeInfo Info;
        Info.Name = QualifiedTypeName<T>();
        Info.OwningModule = ModuleName;
        Info.Kind = Kind;
        Info.Size = sizeof(T);
        Info.Alignment = alignof(T);
        Info.Operations = TTypeOperationsFor<T>::Make();
        return RegisterType(Info);
    }

    // Null when the name is not registered. Name is matched exactly; the registry keys
    // types by the compiler-qualified name interned in Core.
    const FTypeInfo* FindType(const char* Name) const;
    const FTypeInfo* FindType(const FString& Name) const;

    // cv-qualifiers and references are stripped: 'const int' names the same reflected
    // value type as 'int'.
    template <typename T>
    const FTypeInfo* FindType() const
    {
        return FindType(QualifiedTypeName<std::remove_cvref_t<T>>());
    }

    // Removes a type only when ModuleName owns it. Another module's copy of the same
    // descriptor is a no-op and returns true.
    //
    // Returns false, leaving everything untouched, while any other registered type has
    // a property whose TypeName is this type. That includes a type owned by the same
    // module: the module's unregistration pass retries after the referring type is gone.
    // This is what keeps a property's Type pointer from dangling.
    bool UnregisterType(const char* Name, const FString& ModuleName);
    bool UnregisterType(const FString& Name, const FString& ModuleName);

    template <typename T>
    bool UnregisterType(const FString& ModuleName)
    {
        return UnregisterType(QualifiedTypeName<std::remove_cvref_t<T>>(), ModuleName);
    }

    // Removes every type owned by ModuleName, revisiting blocked types until a pass makes
    // no progress. Returns false when something owned by the module is still registered,
    // which means another loaded module refers to it and the library must stay loaded.
    bool UnregisterModuleTypes(const FString& ModuleName);

    // Fills FPropertyInfo::Type for every property whose TypeName is now registered.
    // Called after each registration; also public so a caller can retry explicitly and
    // inspect what remains.
    void ResolveProperties();

    // Number of registered properties whose Type is still null. Engine types have none;
    // a nonzero count after a module's registrations usually means a member type was
    // never registered, or is an array (which is not supported yet).
    int32_t CountUnresolvedProperties() const;

    int32_t GetRegisteredTypeCount() const;
};