#pragma once
#include "Containers/String.h"
#include "Templates/TypeName.h"
#include <type_traits>

struct FTypeInfo;

// ==============================================================================
// Reflected properties
//
// A property is one member of a reflected type: a name, the type of the member,
// and a pair of accessors that turn an instance pointer into the member's address.
//
// Accessors are functions, not stored offsets. An offset would only be correct for
// standard-layout types and would silently break on a class with virtual bases or
// an inherited member that sits at a non-zero offset. The member pointer is the
// source of truth: the compiler computes the adjustment once, in the module that
// knows the complete class definition.
//
// GetMutable is null for a const member. That is the whole read-only signal; no
// separate flag can disagree with the accessor that actually exists. GetConst is
// always present.
//
// Type is filled in by the reflection registry. It points at the metadata for the
// member's value type (cv-qualifiers removed), which may be another struct, a
// primitive, or a pointer.
//
// Lifetime: Name is owned by the registry, not by the module that declared the
// property. The accessors point into the module that instantiated the template;
// the registry removes the property before that module's library is freed.
// ==============================================================================

struct FPropertyInfo
{
    FString Name;

    // Interned identity of the member's value type, produced by TMemberProperty::TypeName().
    // The registry resolves Type from this after every registration for the module has
    // arrived, so the order in which two types are registered does not matter. Null only
    // for a descriptor built without a type identity; those must supply Type directly.
    const char* TypeName = nullptr;

    // Resolved metadata for TypeName. Null while the member type is not registered yet.
    const FTypeInfo* Type = nullptr;

    // Instance -> mutable member address. Null when the member is const.
    void* (*GetMutable)(void* Instance) = nullptr;

    // Instance -> member address. Always present.
    const void* (*GetConst)(const void* Instance) = nullptr;

    bool IsReadOnly() const
    {
        return GetMutable == nullptr;
    }
};

namespace PropertyAccessorDetail
{
    // Turns 'TMember TDeclaringClass::*' into its two types. A pointer to a member
    // inherited from a base keeps the base as TDeclaringClass, which is exactly what
    // the accessor needs to perform the derived-to-base adjustment.
    template <typename T>
    struct TMemberPointerTraits;

    template <typename TDeclaringClass, typename TMember>
    struct TMemberPointerTraits<TMember TDeclaringClass::*>
    {
        using TDeclaringClassType = TDeclaringClass;
        using TMemberType = TMember;
    };
}

// ==============================================================================
// One property binding: a reflected owner type and a pointer to one of its members.
//
//     TMemberProperty<FTransformData, &FTransformData::Location>
//
// TOwner is the type being registered, not the type that declared the member. Both
// are needed: for '&FDerived::InheritedMember' the member pointer's class is the
// base, and the accessor must first convert the owner pointer to that base so the
// compiler applies the correct offset. Casting the void* straight to the base would
// only work when the base happens to sit at offset zero.
//
// TMemberProperty is a compile-time descriptor. It stores nothing; every accessor
// is a static function and TypeName() is a call into Core's interned name table.
// ==============================================================================
template <typename TOwner, auto MemberPointer>
struct TMemberProperty
{
    static_assert(!std::is_const_v<TOwner> && !std::is_volatile_v<TOwner> && !std::is_reference_v<TOwner>,
        "The reflected owner must be an unqualified class type, not a reference.");

    static_assert(std::is_class_v<TOwner>,
        "The reflected owner must be a class or struct.");

    static_assert(std::is_member_object_pointer_v<decltype(MemberPointer)>,
        "A reflected property must point to a non-static data member.");

    using TTraits = PropertyAccessorDetail::TMemberPointerTraits<decltype(MemberPointer)>;
    using TDeclaringClass = typename TTraits::TDeclaringClassType;
    using TMemberType = typename TTraits::TMemberType;

    // The member type as the registry sees it. A 'const int' member is a property of
    // type 'int' that happens to be read-only, not a distinct type.
    using TValueType = std::remove_cv_t<TMemberType>;

    static_assert(std::is_base_of_v<TDeclaringClass, TOwner>,
        "The member's class must be the reflected owner or one of its bases.");

    static_assert(std::is_convertible_v<TOwner*, TDeclaringClass*>,
        "The member's class must be a public, unambiguous base of the reflected owner.");

    static_assert(!std::is_volatile_v<TMemberType>,
        "Volatile properties are not supported: a void* accessor cannot preserve volatile semantics.");

    inline static constexpr bool bIsReadOnly = std::is_const_v<TMemberType>;

    // Interned in Core; identical for every module that reflects this member type.
    static const char* TypeName()
    {
        return QualifiedTypeName<TValueType>();
    }

    static void* GetMutable(void* Instance)
        requires (!std::is_const_v<TMemberType>)
    {
        TOwner* Owner = static_cast<TOwner*>(Instance);
        return &(static_cast<TDeclaringClass*>(Owner)->*MemberPointer);
    }

    static const void* GetConst(const void* Instance)
    {
        const TOwner* Owner = static_cast<const TOwner*>(Instance);
        return &(static_cast<const TDeclaringClass*>(Owner)->*MemberPointer);
    }
};