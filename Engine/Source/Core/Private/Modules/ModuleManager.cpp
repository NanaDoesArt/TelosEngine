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

// Free function behind IMPLEMENT_MODULE's wrapper. Same semantics as the member of the same
// name; it exists so module boilerplate does not need to include ObjectRegistry.h or this header.
bool ReleaseModuleObjects(const char* ModuleName)
{
    return FModuleManager::Get().ReleaseModuleObjects(FString(ModuleName ? ModuleName : ""));
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

    RObject* Scope = FObjectRegistry::Get().CreateScope(ModuleName);
    if (!Scope)
    {
        VENGINE_LOG(LogCore, Error, "[ModuleManager] Failed to create the lifetime scope for '{}'.", ModuleName.CStr());
        return nullptr;
    }

    ModuleScopes.Add(ModuleName, Scope);
    return Scope;
}

bool FModuleManager::ReleaseModuleObjects(const FString& ModuleName)
{
    if (RObject** Found = ModuleScopes.Find(ModuleName))
    {
        RObject* Scope = *Found;
        ModuleScopes.Remove(ModuleName);
        if (Scope)
        {
            // Destroys the whole subtree. Runs on the game thread, while this module's class
            // metadata and DLL are still alive, so every destructor and vtable is valid.
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
            "They belong to classes this module declared but are owned by another module, "
            "so the library must stay loaded and its classes must stay registered.",
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
            VENGINE_LOG(LogCore, Error, "[ModuleManager] Recursive module load rejected for: {}", ModuleName.CStr());
            return nullptr;
        }
        if (Existing->State == EModuleState::Stopping)
        {
            VENGINE_LOG(LogCore, Error, "[ModuleManager] Cannot load module '{}' while it is stopping.", ModuleName.CStr());
            return nullptr;
        }
        if (Existing->State == EModuleState::Failed)
        {
            VENGINE_LOG(LogCore, Error, "[ModuleManager] Cannot load module '{}' because a previous startup attempt failed.", ModuleName.CStr());
            return nullptr;
        }
        return Existing->Instance;
    }

    void* Handle = FPlatformProcess::GetDllHandle(ModuleName);
    if (!Handle)
    {
        VENGINE_LOG(LogCore, Error, "[ModuleManager] Failed to load shared library for module: {}", ModuleName.CStr());
        return nullptr;
    }

    // Every failure path from here on releases whatever the DLL managed to create during its
    // static initialisation (an object created by NewObjectInModule before load completed),
    // then frees the library.
    auto FailLoad = [&](const char* Reason) -> IModuleInterface*
    {
        VENGINE_LOG(LogCore, Error, "[ModuleManager] {} for module: {}", Reason, ModuleName.CStr());
        ReleaseModuleObjects(ModuleName);
        FPlatformProcess::FreeDllHandle(Handle);
        return nullptr;
    };

    auto CreateFunc = reinterpret_cast<CreateModuleFn>(FPlatformProcess::GetDllExport(Handle, "CreateModule"));
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

    LoadedModules.Add(ModuleName, { Handle, ModuleInstance, EModuleState::Loading, false });

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
            "[ModuleManager] StartupModule() threw for '{}'. Deferring cleanup until engine shutdown.",
            ModuleName.CStr()
        );
    }

    if (!bStarted)
    {
        VENGINE_LOG(LogCore, Error, "[ModuleManager] StartupModule() failed for module: {}. Retaining DLL until engine shutdown drains async tasks.", ModuleName.CStr());
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

    VENGINE_LOG(LogCore, Info, "[ModuleManager] Loaded module: {}", ModuleName.CStr());
    return ModuleInstance;
}

bool FModuleManager::UnloadModule(const FString& ModuleName)
{
    FModuleRecord* Record = LoadedModules.Find(ModuleName);
    if (!Record)
    {
        return false;
    }

    if (Record->State == EModuleState::Loading)
    {
        VENGINE_LOG(LogCore, Warning, "[ModuleManager] Rejected unload of '{}' while it is still loading.", ModuleName.CStr());
        return false;
    }

    if (Record->State == EModuleState::Stopping)
    {
        VENGINE_LOG(LogCore, Error, "[ModuleManager] Recursive unload rejected for '{}'.", ModuleName.CStr());
        return false;
    }

    // Phase 1: ShutdownModule() runs once for both Started and Failed modules, so partial startup
    // resources are released while class metadata is still alive. The wrapper additionally
    // releases the module's object scope and unregisters its classes, in that order.
    if (!Record->bShutdownExecuted)
    {
        Record->bShutdownExecuted = true;
        if (Record->State == EModuleState::Started || Record->State == EModuleState::Failed)
        {
            Record->State = EModuleState::Stopping;
            try
            {
                Record->Instance->ShutdownModule();
            }
            catch (...)
            {
                VENGINE_LOG(LogCore, Error, "[ModuleManager] ShutdownModule() threw for '{}'. Continuing with teardown.", ModuleName.CStr());
            }
        }
    }

    // Phase 2: belt and braces. Normally the wrapper already did this; if the wrapper bailed out
    // because of survivors, this call returns false again and we stop here.
    if (!ReleaseModuleObjects(ModuleName))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Refused to release the library for '{}': instances of its classes still survive in other modules.",
            ModuleName.CStr()
        );
        return false;
    }

    // Phase 3: freeing the library while anything of its code is reachable is a guaranteed crash.
    delete Record->Instance;
    FPlatformProcess::FreeDllHandle(Record->Handle);

    LoadedModules.Remove(ModuleName);
    LoadOrder.Remove(ModuleName);
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
                "[ModuleManager] Aborting remaining module unloads because '{}' could not be safely unloaded; preserving lower-level dependencies.",
                LastModule.CStr()
            );
            return;
        }
    }
}