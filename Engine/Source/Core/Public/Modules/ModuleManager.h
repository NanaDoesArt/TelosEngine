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

    int32_t GetLoadedModuleCount() const { return LoadedModules.Num(); }

    // The scope is the root of every object a module creates with
    // NewObjectInModule(). It may be created during DLL static initialization,
    // before LoadModule has inserted a record for the module.
    RObject* GetOrCreateModuleScope(const FString& ModuleName);

    // Destroys the module scope and reports whether instances of its classes
    // still exist elsewhere. The DLL must remain loaded if any survive.
    bool ReleaseModuleObjects(const FString& ModuleName);

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

    TMap<FString, FModuleRecord> LoadedModules;
    TMap<FString, RObject*> ModuleScopes;
    TArray<FString> LoadOrder;
};