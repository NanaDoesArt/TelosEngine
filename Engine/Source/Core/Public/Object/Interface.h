#pragma once
#include "Containers/String.h"

class RObject;

// ==============================================================================
// Reflected interfaces
//
// An interface is a plain class: no base class, no RObject, no data. Pure virtual
// functions and one macro that gives it a name:
//
//     class ISensor
//     {
//         RINTERFACE(ISensor)
//     public:
//         virtual float Read() const = 0;
//     };
//
// A class implements it by inheriting it publicly and listing it once:
//
//     class WSensor : public RObject, public ISensor
//     {
//         RCLASS(WSensor)
//         RIMPLEMENTS(ISensor)
//     public:
//         float Read() const override { return 1.0f; }
//     };
//
// RIMPLEMENTS() is the ONLY thing you write. It is what makes Cast<ISensor>(Object)
// possible: RObject is polymorphic, so the ISensor subobject does not sit at offset 0,
// and only the class's own translation unit knows the real offset. Listing the
// interface there is what lets the compiler generate the adjustment. The generated
// function is called a thunk and is stored next to the class's metadata.
//
// An interface may extend other interfaces:
//
//     class ITunedRadio : public IRadio
//     {
//         RINTERFACE(ITunedRadio, IRadio)
//     public:
//         virtual float GetFrequency() const = 0;
//     };
//
// Listing the bases is all it takes: implementing ITunedRadio automatically registers
// IRadio as well, because the whole point of extending an interface is to promise it.
// ==============================================================================

// One interface implemented by a class.
//
// Name is an owned FString, not a pointer to the interface's string literal: the module
// that declared the interface can be unloaded while a class in another module is still
// registered (nothing records that dependency), and a pointer into a freed DLL is the
// one kind of dangling pointer that can be neither checked nor recovered from.
struct FImplementedInterface
{
    FString Name;

    // Object -> interface subobject. The module that defines the class owns this function,
    // so the class must be unregistered before that module's library is freed.
    void* (*GetAddress)(RObject* Object) = nullptr;
};

// A compile-time list of interface types. CTAD is not used anywhere; the empty list is a
// valid instantiation, which is what makes RINTERFACE(Name) work with no bases.
template <typename... TInterfaces>
struct TInterfaceList
{
};

// Declares the interface's name and its base interfaces. Must be the first thing in the
// interface class body. Expands to nothing but a public type alias and a static function,
// so the class stays a plain abstract class (std::is_abstract, virtual dispatch, etc.).
#define RINTERFACE(InterfaceName, ...) \
public: \
    using InterfaceBases = TInterfaceList<__VA_ARGS__>; \
    static const char* StaticInterfaceName() { return #InterfaceName; } \

// Declares which interfaces a class implements. Go inside the class body, after RCLASS().
// Variadic and non-recursive: no __VA_OPT__, no build flags, plain C++20.
#define RIMPLEMENTS(...) \
public: \
    using InterfaceTypes = TInterfaceList<__VA_ARGS__>; \
private: