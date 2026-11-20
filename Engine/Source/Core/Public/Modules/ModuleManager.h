#pragma once
#include "CoreAPI.h"
#include "Modules/ModuleInterface.h"
#include "Containers/String.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include <cstdint>

class RObject;

enum class EModuleState : uint8_t
{
    Loading,
    Started,
    Stopping,
    Failed
};

class Core_API FModuleManager
{
    public:
    static FModuleManager& Get();

    IModuleInterface* LoadModule(const FString& ModuleName);
    bool UnloadModule(const FString& ModuleName);
    void UnloadAllModules();

    bool IsModuleLoaded(const FString& ModuleName) const;

    // Number of modules currently loaded, including ones that are still resident because their
    // objects survived elsewhere. O(1); for dashboards and diagnostics.
    int32_t GetLoadedModuleCount() const { return LoadedModules.Num(); }

    // ==========================================================================
    // Module lifetime scope.
    // The scope is the root of every object a module creates with NewObjectInModule().
    // It is created on first use (which can be during the DLL's static initialisation,
    // before LoadModule knows about the module) and released when the module unloads.
    // This map is the ONLY list of module scopes in the process: the object registry
    // creates and destroys scopes but never remembers them.
    // ==========================================================================
    RObject* GetOrCreateModuleScope(const FString& ModuleName);

    // Destroys the module's scope, then reports whether any instance of the module's
    // classes survived elsewhere. Returns false if the DLL must stay loaded.
    // Idempotent: safe to call when no scope exists.
    bool ReleaseModuleObjects(const FString& ModuleName);

    private:
    struct FModuleRecord
    {
        void* Handle = nullptr;
        IModuleInterface* Instance = nullptr;
        EModuleState State = EModuleState::Loading;
        bool bShutdownExecuted = false;
    };

    TMap<FString, FModuleRecord> LoadedModules;
    TMap<FString, RObject*> ModuleScopes;
    TArray<FString> LoadOrder;
};