#pragma once

#include "CoreAPI.h"
#include "Modules/ModuleInterface.h"
#include "Containers/String.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include <cstdint>

class Core_API FModuleManager
{
    public:
    static FModuleManager& Get();

    IModuleInterface* LoadModule(const FString& ModuleName);

    // Destroy: unload the library and release the runtime state it produced.
    // Detach:  unload the library but keep the runtime state, for hot reload.
    //
    // Detach is refused before anything is touched when the module did not opt in, when a
    // runtime owns this module's implementation, or when any runtime cannot capture the
    // state. The default is the ordinary, permanent unload.
    bool UnloadModule(const FString& ModuleName, EModuleTeardownMode Mode = EModuleTeardownMode::Destroy);
    void UnloadAllModules();

    bool IsModuleLoaded(const FString& ModuleName) const;

    // True while a module must not create new runtime state: it is stopping, has failed, or
    // its library is already gone but its detached representation remains.
    bool IsModuleUnloading(const FString& ModuleName) const;
    bool IsModuleDetached(const FString& ModuleName) const;

    // Counts modules whose library is loaded and started. Detached records are not loaded.
    int32_t GetLoadedModuleCount() const;

    // ==========================================================================
    // Engine runtimes.
    //
    // A runtime is implemented by an engine module and materializes state on behalf of every
    // module. OwnerModule is that implementing module: the manager keeps the registration
    // callable until the owner's own teardown notification has run, then removes it before
    // FreeDllHandle. The manager never deletes a runtime; the owner does.
    //
    // Register from the runtime owner's PrepareModule. It is a no-op for a runtime that is
    // already registered.
    // ==========================================================================
    bool RegisterRuntime(const FString& OwnerModule, IModuleRuntime* Runtime);
    void UnregisterRuntime(IModuleRuntime* Runtime);

    private:
    struct FModuleRecord
    {
        void* Handle = nullptr;
        IModuleInterface* Instance = nullptr;
        EModuleState State = EModuleState::Loading;

        bool bShutdownExecuted = false;
        bool bShutdownFailed = false;
        bool bFinalizationExecuted = false;
        bool bUnloadInProgress = false;
    };

    struct FRuntimeRecord
    {
        FString OwnerModule;
        IModuleRuntime* Runtime = nullptr;
        
        // Modules for which this runtime was asked to initialize state.
        // They must finish unloading before the runtime owner can unload.
        TArray<FString> ServicedModules;
    };

    struct FUnloadGuard
    {
        FModuleManager& Manager;
        const FString& ModuleName;

        FUnloadGuard(FModuleManager& InManager, const FString& InModuleName)
            : Manager(InManager), ModuleName(InModuleName)
        {
        }

        ~FUnloadGuard()
        {
            if (FModuleRecord* Record = Manager.LoadedModules.Find(ModuleName))
            {
                Record->bUnloadInProgress = false;
            }
        }
    };
    
    void RemoveServicedModule(const FString& ModuleName);
    bool HasServicedModules(const FString& OwnerModule,FString* OutFirstModule = nullptr) const;
    bool NotifyModuleLoaded(const FString& ModuleName);
    bool CanDetachModule(const FString& ModuleName) const;
    bool NotifyModuleTeardown(const FString& ModuleName, EModuleTeardownMode Mode);
    void RemoveRuntimesOwnedBy(const FString& ModuleName);
    bool HasRuntimeOwnedBy(const FString& ModuleName) const;

    TMap<FString, FModuleRecord> LoadedModules;
    TArray<FRuntimeRecord> Runtimes;
    TArray<FString> LoadOrder;
};