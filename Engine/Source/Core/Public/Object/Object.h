#pragma once
#include "CoreAPI.h"
#include "ObjectRegistry.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

class RObject;

class Core_API RObject
{
public:
    using ThisClass = RObject;

    RObject(RObject* InOuter = nullptr);
    virtual ~RObject();

    RObject(const RObject&) = delete;
    RObject& operator=(const RObject&) = delete;
    RObject(RObject&&) = delete;
    RObject& operator=(RObject&&) = delete;

    void SetOuter(RObject* InOuter);
    RObject* GetOuter() const { return Outer; }
    const TArray<RObject*>& GetChildren() const { return OwnedChildren; }
    uint64_t GetUniqueID() const { return UniqueID; }

    static const char* StaticClassName() { return "RObject"; }
    virtual const char* GetClassName() const { return "RObject"; }

    // O(1): the exact FClassInfo* is captured at construction, so this never hashes a name
    // and never calls a virtual function. Returns nullptr only for objects whose registration
    // was rejected (which is Fatal anyway).
    const FClassInfo* GetClass() const { return ClassInfo; }

    bool IsA(const FClassInfo* TargetClass) const
    {
        return ClassInfo && ClassInfo->IsChildOf(TargetClass);
    }

    // Note: the target class is looked up by name (one hash) because caching a FClassInfo*
    // in a static would dangle if that class is ever unregistered and re-registered.
    template <typename T>
    bool IsA() const
    {
        return IsA(FObjectRegistry::Get().FindClass(T::StaticClassName()));
    }

    void DestroyChildren();

    // Single process-wide counter, owned by Core.dll. Never a header static:
    // an 'inline static' member in a header is one counter PER DLL, which produces
    // colliding object IDs across modules.
    static uint64_t AllocateUniqueID();

protected:
    // Protected so that game code cannot construct an RObject anywhere but through the registry.
    // A class that needs a hand-written factory (or a test) exposes its own static MakeInstance(),
    // because a member of a derived class is allowed to call these.
    void* operator new(size_t Size) { return ::operator new(Size); }
    void operator delete(void* Ptr) { ::operator delete(Ptr); }

    friend class FObjectRegistry;
    template <typename TClass>
    friend struct TObjectRegistrar;

private:
    void AddChild(RObject* Child);
    void RemoveChild(RObject* Child);

    RObject* Outer = nullptr;
    TArray<RObject*> OwnedChildren;

    uint64_t UniqueID = 0;
    FClassInfo* ClassInfo = nullptr;   // set by FObjectRegistry::RegisterInstance
};

// Destroys an object and everything it owns. Refuses class default objects.
// Inline on purpose: delete needs access to RObject's protected operator delete, so the real
// implementation lives in FObjectRegistry. Being inline means there is no exported symbol and
// no dll-linkage declaration to keep in sync between the class and the free function.
inline void Destroy(RObject* Obj)
{
    FObjectRegistry::Get().DestroyObject(Obj);
}

// ==============================================================================
// Lifetime scope object
//
// One instance per loaded module. Created by FObjectRegistry::CreateScope() on behalf of
// FModuleManager, which owns the name -> scope map and destroys the scope when the module
// unloads. Every object a module creates with NewObject<T>() outside a constructor hangs off
// this scope, so a module's objects die with the module and never leak.
//
// Not RCLASS'd on purpose: RCLASS installs a per-DLL auto-registrar, and this class must be
// registered exactly once, by Core, on first use (see EnsureBootstrapClasses()).
// ==============================================================================
class Core_API FObjectScope : public RObject
{
public:
    using Super = RObject;
    using ThisClass = FObjectScope;

    FObjectScope() = default;

    // Set by FObjectRegistry::CreateScope(). Used only for diagnostics.
    FString ScopeName;

    static const char* StaticClassName() { return "FObjectScope"; }
    const char* GetClassName() const override { return "FObjectScope"; }
};

// ==============================================================================
// Safe Cross-DLL Cast<T>()
// ==============================================================================

// ==============================================================================
// Safe Cross-DLL Cast<T>()
//
// Cast<To>() works for both kinds of target:
//
//   Cast<WFoo>(Object)    // class cast: walks the registered class chain
//   Cast<IMyFoo>(Object)  // interface cast: uses the thunk emitted by RIMPLEMENTS
//
// Both are null-safe and return nullptr when the object is not of that class / does not implement
// that interface. A class target must derive from RObject; an interface target must declare
// RINTERFACE and the object's own class (or a base class) must list it in RIMPLEMENTS.
// ==============================================================================

template <typename To, typename From>
To* Cast(From* Src)
{
    if (!Src)
    {
        return nullptr;
    }

    if constexpr (std::is_base_of_v<RObject, To>)
    {
        static_assert(std::is_base_of_v<RObject, From>,
            "Cast<RObjectSubclass>() requires an RObject source.");
        return Src->template IsA<To>() ? static_cast<To*>(Src) : nullptr;
    }
    else
    {
        static_assert(std::is_base_of_v<To, From> || std::is_base_of_v<RObject, From>,
            "Cast<Interface>() from another interface supports upcasts only; keep the RObject* and cast from that.");

        if constexpr (std::is_base_of_v<RObject, From>)
        {
            const FClassInfo* ClassInfo = static_cast<const RObject*>(Src)->GetClass();
            if (!ClassInfo)
            {
                return nullptr;
            }
            return static_cast<To*>(ClassInfo->GetInterfaceAddress(
                const_cast<RObject*>(static_cast<const RObject*>(Src)),
                To::StaticInterfaceName()));
        }
        else
        {
            return static_cast<To*>(Src);
        }
    }
}

template <typename To, typename From>
const To* Cast(const From* Src)
{
    return Cast<To>(const_cast<From*>(Src));
}


// ==============================================================================
// Object creation. This is the only creation function game code needs.
//
//   NewObject<T>()        owner = whatever is being constructed right now; if nothing is,
//                                 the module whose code made the call.
//   NewObject<T>(Outer)   owner = Outer.
//
// Inside a constructor the object being built is the only owner with a sane lifetime, so
// default subobjects are created exactly like everything else. Outside a constructor the
// calling module is the only owner you cannot accidentally outlive: the object dies when that
// module unloads. There is deliberately no way to create an object with no owner.
// ==============================================================================
template <typename T>
T* NewObject()
{
    FObjectRegistry& Registry = FObjectRegistry::Get();

    RObject* Owner = Registry.GetConstructionSite();
    if (!Owner)
    {
        // Which DLL compiled this call decides the owner. That is the point: the object cannot
        // outlive the code that made it.
        Owner = FModuleManager::Get().GetOrCreateModuleScope(FString(VENGINE_CURRENT_MODULE_NAME));
    }
    if (!Owner)
    {
        return nullptr;   // GetOrCreateModuleScope() already logged why
    }

    const FClassInfo* RequiredClass = Registry.FindClass(T::StaticClassName());
    return static_cast<T*>(Registry.ConstructObjectOfType(T::StaticClassName(), Owner, RequiredClass));
}

template <typename T>
T* NewObject(RObject* Outer)
{
    FObjectRegistry& Registry = FObjectRegistry::Get();
    const FClassInfo* RequiredClass = Registry.FindClass(T::StaticClassName());
    return static_cast<T*>(Registry.ConstructObjectOfType(T::StaticClassName(), Outer, RequiredClass));
}

// ==============================================================================
// TObjectRegistrar & RCLASS Macro
// ==============================================================================

template <typename TClass>
struct TObjectRegistrar
{
    TObjectRegistrar()
    {
        FModuleClassRegistry::GetEntries().Add({
            [](const FString& ModuleName) -> bool
            {
                return FObjectRegistry::Get().RegisterClass<TClass>(ModuleName)
                    != EClassRegistrationResult::Failed;
            },

            // VENGINE_CURRENT_MODULE_NAME is expanded in the module that compiled this copy
            // of the registrar, which is exactly the module that owns this entry.
            []()
            {
                FObjectRegistry::Get().UnregisterClass<TClass>(
                    VENGINE_CURRENT_MODULE_NAME
                );
            }
        });
    }
};

// RCLASS goes in the class declaration, in a header or in a .cpp.
//
// A class in a header shared by several modules gets one registrar per module, which is
// harmless: registration is idempotent, the declaring module owns the class (dependencies boot
// first), and every other copy is ignored on register and skipped on unregister. A different
// class reusing the same name is rejected as a collision.
//
// One requirement remains: the declaring module needs at least one .cpp that includes the
// header, or the compiler may never emit that module's copy and a consumer could become the
// owner. The failure is conservative (the class is pinned to a loaded module), never dangling.
#define RCLASS(ClassName) \
public: \
    using Super = ThisClass; \
    using ThisClass = ClassName; \
    static const char* StaticClassName() { return #ClassName; } \
    const char* GetClassName() const override { return #ClassName; } \
    friend class FObjectRegistry; \
private: \
    inline static TObjectRegistrar<ClassName> AutoRegistrar{};

// Declares the interfaces this class wants to be queryable with Cast<TInterface>(). Place it
// directly after RCLASS:
//
//     RCLASS(WDevice)
//     RIMPLEMENTS(IMySensor, IMyRadio)
//
// Only needed for interfaces you query by type; calling methods through a pointer you already
// hold does not need it. Listing an interface the class does not inherit is a compile error.
//
// A subclass inherits its parent's list, so it does not repeat it. A subclass that adds an
// interface of its own writes RIMPLEMENTS with only the new ones; the parent's remain reachable
// through the class chain.
#define RIMPLEMENTS(...) \
public: \
    using InterfaceTypes = TInterfaceList<__VA_ARGS__>; \
private: