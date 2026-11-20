#pragma once
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
    // Ordinary modules should normally leave this unchanged.
    virtual bool PrepareModule() { return true; }

    // Must tolerate partial startup when ShutdownModule() is called on failure
    virtual bool StartupModule() { return true; }

    // Must be idempotent and safe to call after either full or partial StartupModule()
    virtual void ShutdownModule() {}
};

// Destroys the calling module's lifetime scope and reports whether any instance of its classes
// survived elsewhere. Called by IMPLEMENT_MODULE's wrapper between the author's ShutdownModule()
// and class unregistration, so a module's objects always die while class metadata and the DLL are
// still alive. Returns false when another module still owns objects of this module's classes; the
// wrapper then leaves the classes registered and the module manager refuses to free the library.
Core_API bool ReleaseModuleObjects(const char* ModuleName);

struct FModuleClassRegistry
{
    using FRegisterFn = bool(*)(const FString& ModuleName);
    using FUnregisterFn = void(*)();

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
            if (Entries[i].Register && !Entries[i].Register(ModuleName))
            {
                // Keep successful registrations until deferred shutdown.
                // The wrapper cleans module resources before unregistering classes.
                return false;
            }
            ++RegisteredCount;
        }
        return true;
    }

    static void UnregisterAll()
    {
        TArray<FEntry>& Entries = GetEntries();
        int32_t& RegisteredCount = GetRegisteredCount();

        for (int32_t i = RegisteredCount - 1; i >= 0; --i)
        {
            if (Entries[i].Unregister)
            {
                Entries[i].Unregister();
            }
        }
        RegisteredCount = 0;
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
    void operator delete(void* ptr) noexcept                        { FMemory::Free(ptr); } \
    void operator delete[](void* ptr) noexcept                      { FMemory::Free(ptr); } \
    void operator delete(void* ptr, size_t) noexcept                { FMemory::Free(ptr); } \
    void operator delete[](void* ptr, size_t) noexcept              { FMemory::Free(ptr); } \
    void operator delete(void* ptr, std::align_val_t) noexcept      { FMemory::Free(ptr); } \
    void operator delete[](void* ptr, std::align_val_t) noexcept    { FMemory::Free(ptr); } \
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
            if (!ReleaseModuleObjects(VENGINE_CURRENT_MODULE_NAME)) \
            { \
                return; \
            } \
            FModuleClassRegistry::UnregisterAll(); \
        } \
    }; \
    extern "C" VENGINE_DLLEXPORT IModuleInterface* CreateModule() \
    { \
        return new FAutoRegisteredModuleWrapper(); \
    }