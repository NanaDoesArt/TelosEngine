
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Object/ObjectRegistry.h"
#include "Logging/Log.h"

using CreateModuleFn = IModuleInterface* (*)();

FModuleManager& FModuleManager::Get()
{
    static FModuleManager Instance;
    return Instance;
}

// Free function behind IMPLEMENT_MODULE's wrapper.
bool ReleaseModuleObjects(const char* ModuleName)
{
    return FModuleManager::Get().ReleaseModuleObjects(
        FString(ModuleName ? ModuleName : "")
    );
}

bool FModuleManager::IsModuleLoaded(const FString& ModuleName) const
{
    const FModuleRecord* Record = LoadedModules.Find(ModuleName);
    return Record && Record->State == EModuleState::Started;
}

RObject* FModuleManager::GetOrCreateModuleScope(const FString& ModuleName)
{
    if (RObject** Existing = ModuleScopes.Find(ModuleName))
    {
        return *Existing;
    }

    // Static initialization before LoadModule inserts its record is allowed.
    // Once teardown begins, however, destructors must not recreate the scope.
    if (const FModuleRecord* Record = LoadedModules.Find(ModuleName))
    {
        if (Record->State == EModuleState::Stopping ||
            Record->State == EModuleState::Failed)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Refused to create a lifetime scope for '{}' during teardown.",
                ModuleName.CStr()
            );
            return nullptr;
        }
    }

    RObject* Scope = FObjectRegistry::Get().CreateScope(ModuleName);
    if (!Scope)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Failed to create the lifetime scope for '{}'.",
            ModuleName.CStr()
        );
        return nullptr;
    }

    ModuleScopes.Add(ModuleName, Scope);
    return Scope;
}

bool FModuleManager::ReleaseModuleObjects(const FString& ModuleName)
{
    // Remove the entry before destroying the scope. This prevents reentrant
    // lookups from returning a scope that is already being destroyed.
    if (RObject** Found = ModuleScopes.Find(ModuleName))
    {
        RObject* Scope = *Found;
        ModuleScopes.Remove(ModuleName);

        if (Scope)
        {
            FObjectRegistry::Get().DestroyScope(Scope);
        }
    }

    int32_t LiveCount = 0;
    if (FObjectRegistry::Get().HasLiveInstancesForModule(ModuleName, &LiveCount))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] '{}' still has {} live object(s) after its scope was released. "
            "They belong to classes this module declared but are owned elsewhere. "
            "The library and its class metadata must remain loaded.",
            ModuleName.CStr(),
            LiveCount
        );
        return false;
    }

    return true;
}

IModuleInterface* FModuleManager::LoadModule(const FString& ModuleName)
{
    if (FModuleRecord* Existing = LoadedModules.Find(ModuleName))
    {
        if (Existing->State == EModuleState::Loading)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Recursive module load rejected for: {}",
                ModuleName.CStr()
            );
            return nullptr;
        }

        if (Existing->State == EModuleState::Stopping)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Cannot load module '{}' while it is stopping.",
                ModuleName.CStr()
            );
            return nullptr;
        }

        if (Existing->State == EModuleState::Failed)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Cannot load module '{}' because a previous startup attempt failed.",
                ModuleName.CStr()
            );
            return nullptr;
        }

        return Existing->Instance;
    }

    void* Handle = FPlatformProcess::GetDllHandle(ModuleName);
    if (!Handle)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Failed to load shared library for module: {}",
            ModuleName.CStr()
        );
        return nullptr;
    }

    auto FailLoad = [&](const char* Reason) -> IModuleInterface*
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] {} for module: {}",
            Reason,
            ModuleName.CStr()
        );

        ReleaseModuleObjects(ModuleName);
        FPlatformProcess::FreeDllHandle(Handle);
        return nullptr;
    };

    auto CreateFunc = reinterpret_cast<CreateModuleFn>(
        FPlatformProcess::GetDllExport(Handle, "CreateModule")
    );

    if (!CreateFunc)
    {
        return FailLoad("CreateModule() not found");
    }

    IModuleInterface* ModuleInstance = nullptr;

    try
    {
        ModuleInstance = CreateFunc();
    }
    catch (...)
    {
        return FailLoad("CreateModule() threw an exception");
    }

    if (!ModuleInstance)
    {
        return FailLoad("CreateModule() returned null");
    }

    LoadedModules.Add(
        ModuleName,
        {
            Handle,
            ModuleInstance,
            EModuleState::Loading,
            false,
            false,
            false,
            false
        }
    );

    bool bStarted = false;

    try
    {
        bStarted = ModuleInstance->StartupModule();
    }
    catch (...)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] StartupModule() threw for '{}'. "
            "Deferring cleanup until engine shutdown.",
            ModuleName.CStr()
        );
    }

    if (!bStarted)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] StartupModule() failed for '{}'. "
            "Retaining DLL until deferred cleanup.",
            ModuleName.CStr()
        );

        if (FModuleRecord* Record = LoadedModules.Find(ModuleName))
        {
            Record->State = EModuleState::Failed;
        }

        LoadOrder.Add(ModuleName);
        return nullptr;
    }

    if (FModuleRecord* Record = LoadedModules.Find(ModuleName))
    {
        Record->State = EModuleState::Started;
    }

    LoadOrder.Add(ModuleName);

    VENGINE_LOG(
        LogCore,
        Info,
        "[ModuleManager] Loaded module: {}",
        ModuleName.CStr()
    );

    return ModuleInstance;
}

bool FModuleManager::UnloadModule(const FString& ModuleName)
{
    FModuleRecord* InitialRecord = LoadedModules.Find(ModuleName);
    if (!InitialRecord)
    {
        return false;
    }
    
    if (InitialRecord->bUnloadInProgress)
    {
        VENGINE_LOG(
            LogCore,
            Warning,
            "[ModuleManager] Recursive unload rejected for '{}'.",
            ModuleName.CStr()
        );
        return false;
    }

    InitialRecord->bUnloadInProgress = true;
    FUnloadGuard UnloadGuard(*this, ModuleName);

    if (InitialRecord->State == EModuleState::Loading)
    {
        VENGINE_LOG(
            LogCore,
            Warning,
            "[ModuleManager] Rejected unload of '{}' while it is still loading.",
            ModuleName.CStr()
        );
        return false;
    }

    if (InitialRecord->bShutdownFailed)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Cannot unload '{}': its ShutdownModule() previously threw. "
            "The module is being kept resident because its cleanup state is unknown.",
            ModuleName.CStr()
        );
        return false;
    }

    // Copy stable values before invoking module code. Never retain a pointer
    // into LoadedModules across an external callback.
    IModuleInterface* Instance = InitialRecord->Instance;
    void* Handle = InitialRecord->Handle;

    // Phase 1: run author shutdown exactly once.
    if (!InitialRecord->bShutdownExecuted)
    {
        InitialRecord->bShutdownExecuted = true;
        InitialRecord->State = EModuleState::Stopping;

        try
        {
            Instance->ShutdownModule();
        }
        catch (...)
        {
            // Reacquire the record; callbacks may have mutated the map.
            if (FModuleRecord* Record = LoadedModules.Find(ModuleName))
            {
                if (Record->Instance == Instance)
                {
                    Record->bShutdownFailed = true;
                }
            }

            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] ShutdownModule() threw for '{}'. "
                "Keeping the DLL loaded because cleanup cannot be verified.",
                ModuleName.CStr()
            );
            return false;
        }
    }
    else
    {
        // A retry is permitted only after a previous shutdown completed.
        if (FModuleRecord* Record = LoadedModules.Find(ModuleName))
        {
            if (Record->Instance != Instance)
            {
                return false;
            }
            Record->State = EModuleState::Stopping;
        }
        else
        {
            return false;
        }
    }

    // Reacquire after the callback. Do not assume the original map entry
    // pointer remains valid.
    FModuleRecord* Record = LoadedModules.Find(ModuleName);
    if (!Record || Record->Instance != Instance || Record->Handle != Handle)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Module record for '{}' changed during shutdown.",
            ModuleName.CStr()
        );
        return false;
    }

    // Phase 2: destroy module-owned objects and check for survivors.
    // This is intentionally repeated on retry. The scope removal is idempotent.
    if (!ReleaseModuleObjects(ModuleName))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Refused to release '{}' because instances of its classes survive.",
            ModuleName.CStr()
        );
        return false;
    }

    // Phase 3: unregister module metadata only after no instances remain.
    // The wrapper's implementation performs reverse-order unregistration.
    Record = LoadedModules.Find(ModuleName);
    if (!Record || Record->Instance != Instance || Record->Handle != Handle)
    {
        return false;
    }

    if (!Record->bFinalizationExecuted)
    {
        try
        {
            if (!Instance->FinalizeModule())
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ModuleManager] Finalization of '{}' did not complete. "
                    "Keeping the DLL loaded for a later retry.",
                    ModuleName.CStr()
                );
                return false;
            }
        }
        catch (...)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] FinalizeModule() threw for '{}'. "
                "Keeping the DLL loaded.",
                ModuleName.CStr()
            );
            return false;
        }

        Record = LoadedModules.Find(ModuleName);
        if (!Record || Record->Instance != Instance || Record->Handle != Handle)
        {
            return false;
        }

        Record->bFinalizationExecuted = true;
    }

    // Phase 4: destroy the module instance while its DLL is still resident.
    // The record remains in Stopping during its destructor, preventing a
    // reentrant LoadModule() from loading a second copy.
    delete Instance;

    Record = LoadedModules.Find(ModuleName);
    if (!Record || Record->Instance != Instance || Record->Handle != Handle)
    {
        return false;
    }

    LoadedModules.Remove(ModuleName);
    LoadOrder.Remove(ModuleName);

    FPlatformProcess::FreeDllHandle(Handle);
    return true;
}

void FModuleManager::UnloadAllModules()
{
    while (!LoadOrder.IsEmpty())
    {
        FString LastModule = LoadOrder[LoadOrder.Num() - 1];

        if (!UnloadModule(LastModule))
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Aborting remaining module unloads because '{}' "
                "could not be safely unloaded; preserving lower-level dependencies.",
                LastModule.CStr()
            );
            return;
        }
    }
}
