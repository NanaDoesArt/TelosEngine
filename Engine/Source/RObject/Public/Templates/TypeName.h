#pragma once
#include "CoreAPI.h"

// ==============================================================================
// Compiler-derived type names
//
// The name a class or interface is registered under must:
//   - be unique across the whole process, and
//   - be identical in every module that compiles the same type.
//
// The authored name (#ClassName) is neither. Two interfaces called IInteractable in
// different namespaces are different types but have the same authored name, and a
// registry keyed by it hands out the wrong interface subobject without complaining.
//
// The compiler already knows the namespace-qualified name, so take it from the compiler
// instead of asking people to type a prefix by hand. QualifiedTypeName<T>() returns that
// name, interned in Core: the pointer is stable for the life of the process, so each
// module can cache it in a function-local static and every later call is an array read.
//
// Only the string comparison is used. No RTTI, no type_info, no build flags.
//
// One case cannot be solved by the compiler: a type declared inside an anonymous namespace
// has no name that is unique across translation units. The anonymous marker is stripped, so
// the result is stable (every TU spells such a type the same way) but not unique: two
// different TU-local types with the same name look like repeated registrations of one class.
// That is fine for a test fixture with a distinctive name, and wrong for anything else. Give
// a class or interface a named namespace when its name could be used twice.
// ==============================================================================

#if defined(_MSC_VER)
    #define VENGINE_FUNCTION_SIGNATURE __FUNCSIG__
#else
    #define VENGINE_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

// Parses the compiler's signature string, strips the class-key and any anonymous
// namespace, then interns the result in Core. Never returns null. Defined in Core so
// every module shares one string table and one pointer per type.
RObject_API const char* InternQualifiedTypeName(const char* RawSignature);

template <typename T>
const char* QualifiedTypeName()
{
    // A pointer, not a string object: nothing runs at DLL unload, and the storage lives
    // in Core, which outlives every other module.
    static const char* Cached = InternQualifiedTypeName(VENGINE_FUNCTION_SIGNATURE);
    return Cached;
}