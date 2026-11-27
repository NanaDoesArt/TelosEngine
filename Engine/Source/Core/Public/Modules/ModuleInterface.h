#pragma once

#include "CoreAPI.h"
#include "HAL/Memory.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include <cstdint>
#include <new>

#ifdef _WIN32
    #define VENGINE_DLLEXPORT __declspec(dllexport)
#else
    #define VENGINE_DLLEXPORT __attribute__((visibility("default")))
#endif

#ifndef VENGINE_CURRENT_MODULE_NAME
    #define VENGINE_CURRENT_MODULE_NAME "UnknownModule"
#endif

// ==============================================================================
// Module lifetime vocabulary
//
// These enums are shared by the loader (Core) and by engine runtimes that keep
// state on a module's behalf. They live here, with IModuleInterface, so a module
// author reads one header and never a private one.
// ==============================================================================

// What the loader currently knows about a module.
//
// Loading / Started / Stopping / Failed are the ordinary lifecycle.
//
// Detaching / Detached / Attaching belong to hot reload: the library leaves the
// process while the runtime representation its blueprints produced stays in the
// engine. Detached is the only state in which the module is not loaded but still
// has an engine-owned record.
enum class EModuleState : uint8_t
{
    Loading,    // library mapped, module object created, startup in progress
    Started,    // author startup finished; the module is live
    Detaching,  // hot reload: runtime state is being captured, library still mapped
    Detached,   // hot reload: library unloaded, runtime representation retained
    Attaching,  // a detached module's library is back; blueprints are merging
    Stopping,   // permanent teardown in progress
    Failed      // startup failed; library retained for a later cleanup attempt
};

// What happens to the runtime state a module's blueprints produced.
//
// Destroy: the state is going away with the module (engine exit, plugin removed).
// Detach:  the code is going away, the state stays. Every reference into the
//          library must be dropped, and nothing may execute until a new library
//          attaches and the state is reconciled.
enum class EModuleTeardownMode : uint8_t
{
    Destroy,
    Detach
};

// ==============================================================================
// Engine-side runtime hook
//
// This is NOT implemented by modules. It is implemented by an engine subsystem
// that materializes runtime state on behalf of every module (today: the object
// runtime). Core never learns what that state is; it only asks whether it may be
// preserved.
//
// Ownership and ordering:
//
//   fresh load:  OnModuleLoaded -> PrepareModule/registrations/StartupModule
//   reattach:    OnModuleLoaded -> registrations -> OnModuleReattached -> StartupModule
//   teardown:    [CanDetachModule] -> ShutdownModule -> OnModuleTeardown -> FinalizeModule (Destroy only)
//
// The manager stores an owner module name with every runtime and removes the
// registration right after that owner's teardown notification, before the
// owner's library is freed. A runtime must therefore never be called after its
// owner is gone. The manager never deletes a runtime: the owner does.
//
// A runtime that cannot preserve a module's state must answer false from
// CanDetachModule(). That default is deliberate: a module cannot be detached by
// accident, only by an engine runtime that knows how to capture every value.
// ==============================================================================
class IModuleRuntime
{
public:
    virtual ~IModuleRuntime() = default;

    // Fresh library, module record exists, before PrepareModule() and before any
    // of the module's contributions are registered. Return false to fail the load.
    virtual bool OnModuleLoaded(const FString& ModuleName) { return true; }

    // Preflight for hot reload. Must not mutate anything: it is asked before the
    // module's ShutdownModule() runs, and a false answer must leave the module
    // exactly as it was.
    //
    // Return false when any live state cannot be represented without the module's
    // code (native instances that cannot be captured, subclasses in other modules,
    // code references that cannot be dropped). The default is false.
    virtual bool CanDetachModule(const FString& ModuleName) { return false; }

    // Called after ShutdownModule(), before metadata unregistration and before the
    // library is freed.
    //
    // Detach:  capture everything that outlives the library into runtime-owned
    //          storage, destroy or freeze the native representation, and drop every
    //          code reference into the library. The representation stays readable.
    // Destroy: release everything this module owns. When the module is already
    //          Detached there is no library: no module code may run, only
    //          runtime-owned data is discarded.
    //
    // false keeps the library (Destroy) or aborts the detach before the handle is
    // freed (Detach). A retry is allowed.
    virtual bool OnModuleTeardown(const FString& ModuleName, EModuleTeardownMode Mode) = 0;

    // A detached module's library is back and its blueprints have merged. Called
    // before StartupModule() so the author starts from a rehydrated world.
    virtual bool OnModuleReattached(const FString& ModuleName) { return true; }
};

// ==============================================================================
// Module author interface
//
// Signatures never change: generated module boilerplate must keep compiling.
// New capabilities are additive virtuals with safe defaults.
// ==============================================================================
class IModuleInterface
{
public:
    virtual ~IModuleInterface() = default;

    // Establishes foundational registration required by automatic classes.
    virtual bool PrepareModule() { return true; }

    // May be called after either full or partial StartupModule().
    // The manager invokes this at most once per library load. On a reattach it is
    // called again, after OnModuleReattached(), so check FModuleManager for the
    // state if the module must not rebuild what the runtime just restored.
    virtual bool StartupModule() { return true; }

    // Author-defined shutdown. Called once per library load, before object destruction.
    // In Detach mode the library is about to leave: release everything that cannot be
    // captured and expect to run StartupModule() again after recompilation.
    virtual void ShutdownModule() {}

    // Called after the manager has destroyed the module scope and confirmed
    // that no instances of this module's classes remain.
    //
    // Not called on Detach: the class metadata and runtime records survive, and the
    // library's own contribution table dies with the library.
    //
    // Implementations should be retry-safe if they can return false.
    virtual bool FinalizeModule() { return true; }

    // Hot reload opt-in.
    //
    // A module may only be detached if its author says it holds no state that
    // cannot be captured by the engine runtime, and every registered runtime
    // agrees. Default false: generated modules are safe by default and can never
    // be detached by accident.
    virtual bool SupportsDetach() const { return false; }
};

// ==============================================================================
// Module contributions
//
// A module publishes work that Core performs on its behalf at a fixed point in the
// module's life. Core only sees two function pointers; what they do is not Core's
// business. Object class registration is the only contribution today.
//
// The table is a function-local static, so each library gets its own: entries added
// by a module's static initialization are read back by that same library's wrapper.
// ==============================================================================
struct FModuleClassRegistry
{
    using FRegisterFn = bool(*)(const FString& ModuleName);

    // Returns false when the entry cannot be removed yet (another registered class still
    // derives from it). The DLL must stay loaded, and a later retry can finish the job.
    using FUnregisterFn = bool(*)();

    struct FEntry
    {
        FRegisterFn Register;
        FUnregisterFn Unregister;
    };

    static TArray<FEntry>& GetEntries()
    {
        static TArray<FEntry> Entries;
        return Entries;
    }

    static int32_t& GetRegisteredCount()
    {
        static int32_t RegisteredCount = 0;
        return RegisteredCount;
    }

    static bool RegisterAll(const FString& ModuleName)
    {
        TArray<FEntry>& Entries = GetEntries();
        int32_t& RegisteredCount = GetRegisteredCount();

        for (int32_t i = RegisteredCount; i < Entries.Num(); ++i)
        {
            if (Entries[i].Register &&
                !Entries[i].Register(ModuleName))
            {
                // Successfully registered entries remain registered.
                // Deferred shutdown will clean them up.
                return false;
            }

            ++RegisteredCount;
        }

        return true;
    }

    static bool UnregisterAll()
    {
        TArray<FEntry>& Entries = GetEntries();
        int32_t& RegisteredCount = GetRegisteredCount();

        // Entries are removed in reverse registration order, which normally puts a subclass
        // before its base. That order is not guaranteed (static initialization order across
        // translation units is arbitrary), and a class whose base is still registered refuses
        // to unregister. Such entries are kept and retried after the rest of the pass, so a
        // class chain of any depth converges within a few passes.
        //
        // A class that is still blocked at the end of a pass with no progress belongs to
        // another loaded module. Report failure and keep the entries so the caller can retry
        // after that module unloads; the caller must also keep this DLL loaded.
        while (RegisteredCount > 0)
        {
            TArray<FEntry> Pending;
            bool bProgress = false;

            for (int32_t Index = RegisteredCount - 1; Index >= 0; --Index)
            {
                const FEntry& Entry = Entries[Index];
                bool bUnregistered = true;

                try
                {
                    if (Entry.Unregister)
                    {
                        bUnregistered = Entry.Unregister();
                    }
                }
                catch (...)
                {
                    bUnregistered = false;
                }

                if (bUnregistered)
                {
                    bProgress = true;
                }
                else
                {
                    Pending.Add(Entry);
                }
            }

            if (Pending.Num() == 0)
            {
                RegisteredCount = 0;
                return true;
            }

            Entries = Pending;
            RegisteredCount = Pending.Num();

            if (!bProgress)
            {
                return false;
            }
        }

        return true;
    }
};

#define IMPLEMENT_MODULE(ModuleClass) \
    void* operator new(size_t size) \
    { \
        void* Ptr = FMemory::Malloc(size); \
        if (!Ptr) throw std::bad_alloc(); \
        return Ptr; \
    } \
    void* operator new[](size_t size) \
    { \
        void* Ptr = FMemory::Malloc(size); \
        if (!Ptr) throw std::bad_alloc(); \
        return Ptr; \
    } \
    void* operator new(size_t size, std::align_val_t al) \
    { \
        void* Ptr = FMemory::Malloc(size, static_cast<uint32_t>(al)); \
        if (!Ptr) throw std::bad_alloc(); \
        return Ptr; \
    } \
    void* operator new[](size_t size, std::align_val_t al) \
    { \
        void* Ptr = FMemory::Malloc(size, static_cast<uint32_t>(al)); \
        if (!Ptr) throw std::bad_alloc(); \
        return Ptr; \
    } \
    void operator delete(void* ptr) noexcept                   { FMemory::Free(ptr); } \
    void operator delete[](void* ptr) noexcept                 { FMemory::Free(ptr); } \
    void operator delete(void* ptr, size_t) noexcept           { FMemory::Free(ptr); } \
    void operator delete[](void* ptr, size_t) noexcept         { FMemory::Free(ptr); } \
    void operator delete(void* ptr, std::align_val_t) noexcept { FMemory::Free(ptr); } \
    void operator delete[](void* ptr, std::align_val_t) noexcept { FMemory::Free(ptr); } \
    class FAutoRegisteredModuleWrapper : public ModuleClass \
    { \
    public: \
        bool StartupModule() override \
        { \
            if (!ModuleClass::PrepareModule()) \
            { \
                return false; \
            } \
            if (!FModuleClassRegistry::RegisterAll(VENGINE_CURRENT_MODULE_NAME)) \
            { \
                return false; \
            } \
            return ModuleClass::StartupModule(); \
        } \
        void ShutdownModule() override \
        { \
            ModuleClass::ShutdownModule(); \
        } \
        bool FinalizeModule() override \
        { \
            if (!ModuleClass::FinalizeModule()) \
            { \
                return false; \
            } \
    return FModuleClassRegistry::UnregisterAll(); \
}\
    }; \
    extern "C" VENGINE_DLLEXPORT IModuleInterface* CreateModule() \
    { \
        return new FAutoRegisteredModuleWrapper(); \
    }