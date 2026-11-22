#pragma once

#include "CoreAPI.h"
#include "HAL/Memory.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include <new>

#ifdef _WIN32
    #define VENGINE_DLLEXPORT __declspec(dllexport)
#else
    #define VENGINE_DLLEXPORT __attribute__((visibility("default")))
#endif

#ifndef VENGINE_CURRENT_MODULE_NAME
    #define VENGINE_CURRENT_MODULE_NAME "UnknownModule"
#endif

class IModuleInterface
{
public:
    virtual ~IModuleInterface() = default;

    // Establishes foundational registration required by automatic classes.
    virtual bool PrepareModule() { return true; }

    // May be called after either full or partial StartupModule().
    // The manager invokes this at most once.
    virtual bool StartupModule() { return true; }

    // Author-defined shutdown. Called once, before object destruction.
    virtual void ShutdownModule() {}

    // Called after the manager has destroyed the module scope and confirmed
    // that no instances of this module's classes remain.
    //
    // Implementations should be retry-safe if they can return false.
    virtual bool FinalizeModule() { return true; }
};

// Destroys the calling module's lifetime scope and reports whether any instance
// of its classes survived elsewhere.
Core_API bool ReleaseModuleObjects(const char* ModuleName);

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
            return FModuleClassRegistry::UnregisterAll(); \
        } \
    }; \
    extern "C" VENGINE_DLLEXPORT IModuleInterface* CreateModule() \
    { \
        return new FAutoRegisteredModuleWrapper(); \
    }