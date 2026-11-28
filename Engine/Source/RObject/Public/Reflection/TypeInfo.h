#pragma once
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Reflection/PropertyInfo.h"
#include "Reflection/TypeOperations.h"
#include <cstddef>
#include <cstdint>

// ==============================================================================
// Reflected type kinds
//
// Primitive covers every value that has no reflected structure of its own:
// arithmetic types, enums that are described only as values, and pointers. A
// pointer is deliberately not a distinct kind yet: whether it owns something is a
// property of the member, not of the pointer type, and reference tracking is not
// part of this layer.
//
// Struct is an ordinary C++ class or struct value type. Class is reserved for a
// type that also has object-runtime semantics; only the RObject registration in a
// later stage marks a type as Class. C++ itself cannot tell 'class' from 'struct',
// so the kind is chosen by the registration, not guessed from the type.
//
// Enum exists from the first stage so an enum can be described by its underlying
// value type without pretending to be a struct with no properties. Enum value
// lists and bit flags are a later consumer.
// ==============================================================================

enum class ETypeKind : uint8_t
{
    Primitive,
    Enum,
    Struct,
    Class
};

// ==============================================================================
// Reflected type metadata
//
// The central description of one C++ type. It says what the type is, how large it
// is, and what operations its values support. RObject is not mentioned anywhere:
// an ordinary struct gets an FTypeInfo and never participates in object ownership.
//
// Operations are stored by value, not by pointer. The registry owns FTypeInfo
// objects, so a pointer would either point back into module static storage (which
// dies first) or require a second engine-owned table whose address must stay
// stable. Four function pointers are cheap to copy, and a by-value member cannot
// dangle. The pointers themselves still belong to the module that instantiated
// TTypeOperationsFor; the registry removes the type before that library is freed.
//
// Name and OwningModule are FString copies owned by the registry. They must not be
// pointers to literals in the declaring module, because the metadata can outlive a
// consumer's view of that module during teardown.
//
// Properties are in declaration order as the registration supplied them. They are
// not sorted: an inspector or serializer that wants a stable order can sort names,
// while a human-authored list stays readable in the order it was written.
// ==============================================================================

struct FTypeInfo
{
    FString Name;
    FString OwningModule;

    ETypeKind Kind = ETypeKind::Primitive;

    std::size_t Size = 0;
    std::size_t Alignment = 0;

    FTypeOperations Operations;

    TArray<FPropertyInfo> Properties;

    // Returns null when the property does not exist. The pointer is valid until the
    // type's metadata is removed or replaced by a new registration; registered
    // metadata is immutable while it is live.
    const FPropertyInfo* FindProperty(const char* PropertyName) const
    {
        if (!PropertyName)
        {
            return nullptr;
        }

        for (const FPropertyInfo& Property : Properties)
        {
            if (Property.Name == PropertyName)
            {
                return &Property;
            }
        }

        return nullptr;
    }
};