#pragma once
#include <concepts>
#include <new>
#include <type_traits>

// ==============================================================================
// Reflected value operations
//
// The smallest set of things a reflected value has to support: create it, destroy
// it, assign one value over another, and ask whether two values are equal.
//
// A property descriptor does not know how to do any of that. It points at the
// operations of its type, which the template below generates from ordinary C++
// expressions. int, FString and FTransformData all travel through the same
// mechanism; their descriptors differ only in which operations exist.
//
// Every operation is optional. A type with no default constructor gets no
// Construct. A type that cannot be copy-assigned gets no Copy. A type with no
// equality operator gets no Identical. Consumers must check for null before
// calling, which keeps the metadata honest about what a type can actually do
// instead of forcing every reflected type to satisfy every operation.
//
// Identical is deliberately NOT a memcmp fallback for types without operator==.
// Comparing object representation also compares padding bytes, so two equal
// values could be reported as different. Types that want equality define it.
//
// Lifetime: the function pointers point into the module that instantiated the
// template. The reflection registry copies the whole struct by value into its own
// metadata, so the metadata does not live in a module's heap. The code still
// belongs to that module: the registry must remove the type before the owning
// module's library is freed, exactly as it does for class metadata.
// ==============================================================================

struct FTypeOperations
{
    // Constructs a T at Memory, which must be aligned for T and uninitialized.
    // Null when T is not default-constructible.
    void (*Construct)(void* Memory) = nullptr;

    // Destroys a T previously constructed at Memory. Null when T is not destructible.
    void (*Destruct)(void* Memory) = nullptr;

    // Assigns Source over an already-constructed Destination. It does not construct or
    // destroy either side: pair it with Construct and Destruct when working with raw
    // storage. Null when T is not copy-assignable.
    void (*Copy)(void* Destination, const void* Source) = nullptr;

    // True when the values are equal according to T's operator==. Null when T has no
    // equality operator whose result is convertible to bool.
    bool (*Identical)(const void* A, const void* B) = nullptr;
};

// Operations for one value type. Make() is cheap: it fills a small struct with
// pointers to the member functions that this type actually supports.
template <typename T>
struct TTypeOperationsFor
{
    static_assert(!std::is_void_v<T>, "A reflected value type cannot be void.");
    static_assert(!std::is_reference_v<T>, "Reflect the referred-to type, not a reference.");
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
        "Reflect the unqualified value type, not a cv-qualified one.");

    static void ConstructDefault(void* Memory)
    {
        ::new (Memory) T();
    }

    static void Destroy(void* Memory)
    {
        static_cast<T*>(Memory)->~T();
    }

    static void Assign(void* Destination, const void* Source)
    {
        *static_cast<T*>(Destination) = *static_cast<const T*>(Source);
    }

    static bool Equals(const void* A, const void* B)
    {
        return *static_cast<const T*>(A) == *static_cast<const T*>(B);
    }

    static FTypeOperations Make()
    {
        FTypeOperations Operations;

        if constexpr (std::is_default_constructible_v<T>)
        {
            Operations.Construct = &ConstructDefault;
        }

        if constexpr (std::is_destructible_v<T>)
        {
            Operations.Destruct = &Destroy;
        }

        if constexpr (std::is_copy_assignable_v<T>)
        {
            Operations.Copy = &Assign;
        }

        if constexpr (requires(const T& A, const T& B)
                      {
                          { A == B } -> std::convertible_to<bool>;
                      })
        {
            Operations.Identical = &Equals;
        }

        return Operations;
    }
};