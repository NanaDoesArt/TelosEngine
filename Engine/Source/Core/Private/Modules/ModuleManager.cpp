
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Logging/Log.h"

using CreateModuleFn = IModuleInterface* (*)();

namespace
{
    // ModuleManager currently assumes that lifecycle operations are serialized.
    // Runtime callbacks must not recursively load/unload modules or modify the
    // runtime registration list.
    //
    // This is a reentrancy guard, not a thread-synchronization primitive.
    bool GIsDispatchingRuntimeCallbacks = false;

    class FRuntimeCallbackGuard
    {
    public:
        FRuntimeCallbackGuard()
            : bPreviousValue(GIsDispatchingRuntimeCallbacks)
        {
            GIsDispatchingRuntimeCallbacks = true;
        }

        ~FRuntimeCallbackGuard()
        {
            GIsDispatchingRuntimeCallbacks = bPreviousValue;
        }

        FRuntimeCallbackGuard(const FRuntimeCallbackGuard&) = delete;
        FRuntimeCallbackGuard& operator=(const FRuntimeCallbackGuard&) = delete;

    private:
        bool bPreviousValue;
    };
}

FModuleManager& FModuleManager::Get()
{
    static FModuleManager Instance;
    return Instance;
}

// ============================================================================
// Queries
// ============================================================================

bool FModuleManager::IsModuleLoaded(const FString& ModuleName) const
{
    const FModuleRecord* Record = LoadedModules.Find(ModuleName);

    return Record &&
        Record->State == EModuleState::Started &&
        Record->Handle != nullptr &&
        Record->Instance != nullptr;
}

bool FModuleManager::IsModuleUnloading(const FString& ModuleName) const
{
    const FModuleRecord* Record = LoadedModules.Find(ModuleName);

    if (!Record)
    {
        return false;
    }

    switch (Record->State)
    {
        case EModuleState::Stopping:
        case EModuleState::Failed:
        case EModuleState::Detaching:
        case EModuleState::Detached:
        case EModuleState::Attaching:
            return true;

        default:
            return false;
    }
}

bool FModuleManager::IsModuleDetached(const FString& ModuleName) const
{
    const FModuleRecord* Record = LoadedModules.Find(ModuleName);

    return Record && Record->State == EModuleState::Detached;
}

int32_t FModuleManager::GetLoadedModuleCount() const
{
    int32_t Count = 0;

    for (const FString& ModuleName : LoadOrder)
    {
        if (IsModuleLoaded(ModuleName))
        {
            ++Count;
        }
    }

    return Count;
}

// ============================================================================
// Runtime registration
// ============================================================================

bool FModuleManager::RegisterRuntime(
    const FString& OwnerModule,
    IModuleRuntime* Runtime)
{
    if (GIsDispatchingRuntimeCallbacks)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime registration rejected during a runtime callback."
        );
        return false;
    }

    if (!Runtime)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Cannot register a null runtime for '{}'.",
            OwnerModule.CStr()
        );
        return false;
    }

    FModuleRecord* OwnerRecord = LoadedModules.Find(OwnerModule);

    // Runtime registration belongs in the implementing module's
    // PrepareModule() phase.
    if (!OwnerRecord ||
        OwnerRecord->State != EModuleState::Loading ||
        !OwnerRecord->Instance)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime owner '{}' is not in its registration phase.",
            OwnerModule.CStr()
        );
        return false;
    }

    // Idempotent registration of the same runtime.
    for (const FRuntimeRecord& Existing : Runtimes)
    {
        if (Existing.Runtime == Runtime)
        {
            return Existing.OwnerModule == OwnerModule;
        }

        if (Existing.OwnerModule == OwnerModule)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Module '{}' already owns a different runtime.",
                OwnerModule.CStr()
            );
            return false;
        }
    }

    // Runtime implementations must be registered before the modules they
    // service. This ensures they receive every subsequent OnModuleLoaded
    // notification and avoids trying to reconstruct unknown historical state.
    if (!LoadOrder.IsEmpty())
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime '{}' registered too late. "
            "Runtime modules must load before modules that depend on them.",
            OwnerModule.CStr()
        );
        return false;
    }

    Runtimes.Add({ OwnerModule, Runtime });

    VENGINE_LOG(
        LogCore,
        Info,
        "[ModuleManager] Registered runtime owned by '{}'.",
        OwnerModule.CStr()
    );

    return true;
}

void FModuleManager::UnregisterRuntime(IModuleRuntime* Runtime)
{
    if (!Runtime)
    {
        return;
    }

    if (GIsDispatchingRuntimeCallbacks)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime unregistration rejected during a runtime callback."
        );
        return;
    }

    for (int32_t Index = Runtimes.Num() - 1; Index >= 0; --Index)
    {
        if (Runtimes[Index].Runtime == Runtime)
        {
            Runtimes.RemoveAt(Index);
            return;
        }
    }
}

void FModuleManager::RemoveRuntimesOwnedBy(const FString& ModuleName)
{
    for (int32_t Index = Runtimes.Num() - 1; Index >= 0; --Index)
    {
        if (Runtimes[Index].OwnerModule == ModuleName)
        {
            Runtimes.RemoveAt(Index);
        }
    }
}

bool FModuleManager::HasRuntimeOwnedBy(const FString& ModuleName) const
{
    for (const FRuntimeRecord& Runtime : Runtimes)
    {
        if (Runtime.OwnerModule == ModuleName)
        {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Runtime notifications
// ============================================================================

bool FModuleManager::NotifyModuleLoaded(const FString& ModuleName)
{
    FRuntimeCallbackGuard CallbackGuard;

    // Copy the records so no reference into Runtimes survives an external
    // callback. Registration changes are rejected while dispatching.
    const TArray<FRuntimeRecord> RuntimeSnapshot = Runtimes;

    for (const FRuntimeRecord& Entry : RuntimeSnapshot)
    {
        if (!Entry.Runtime)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Encountered a null runtime registration."
            );
            return false;
        }

        // The runtime's own module creates its own scope during
        // RegisterObjectRuntime(). It must not receive this notification.
        if (Entry.OwnerModule == ModuleName)
        {
            continue;
        }

        bool bSuccess = false;

        try
        {
            bSuccess = Entry.Runtime->OnModuleLoaded(ModuleName);
        }
        catch (...)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Runtime owned by '{}' threw while "
                "processing module '{}'.",
                Entry.OwnerModule.CStr(),
                ModuleName.CStr()
            );
        }

        if (!bSuccess)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Runtime owned by '{}' rejected module '{}'.",
                Entry.OwnerModule.CStr(),
                ModuleName.CStr()
            );
            return false;
        }
    }

    return true;
}

bool FModuleManager::CanDetachModule(const FString& ModuleName) const
{
    const FModuleRecord* Record = LoadedModules.Find(ModuleName);

    if (!Record ||
        !Record->Instance ||
        !Record->Instance->SupportsDetach())
    {
        return false;
    }

    FRuntimeCallbackGuard CallbackGuard;

    for (const FRuntimeRecord& Entry : Runtimes)
    {
        if (!Entry.Runtime)
        {
            return false;
        }

        bool bCanDetach = false;

        try
        {
            bCanDetach = Entry.Runtime->CanDetachModule(ModuleName);
        }
        catch (...)
        {
            bCanDetach = false;
        }

        if (!bCanDetach)
        {
            return false;
        }
    }

    return true;
}

bool FModuleManager::NotifyModuleTeardown(
    const FString& ModuleName,
    EModuleTeardownMode Mode)
{
    FRuntimeCallbackGuard CallbackGuard;

    const TArray<FRuntimeRecord> RuntimeSnapshot = Runtimes;

    bool bAllSucceeded = true;

    // Do not stop at the first failure. Other runtimes may still be able
    // to release their state safely.
    //
    // OnModuleTeardown must therefore be idempotent and retry-safe.
    for (const FRuntimeRecord& Entry : RuntimeSnapshot)
    {
        if (!Entry.Runtime)
        {
            bAllSucceeded = false;
            continue;
        }

        bool bSuccess = false;

        try
        {
            bSuccess = Entry.Runtime->OnModuleTeardown(ModuleName, Mode);
        }
        catch (...)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Runtime owned by '{}' threw while "
                "tearing down module '{}'.",
                Entry.OwnerModule.CStr(),
                ModuleName.CStr()
            );
        }

        if (!bSuccess)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Runtime owned by '{}' could not "
                "complete teardown of '{}'.",
                Entry.OwnerModule.CStr(),
                ModuleName.CStr()
            );

            bAllSucceeded = false;
        }
    }

    return bAllSucceeded;
}

// ============================================================================
// Loading
// ============================================================================

IModuleInterface* FModuleManager::LoadModule(const FString& ModuleName)
{
    if (GIsDispatchingRuntimeCallbacks)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Module loading rejected during a runtime callback."
        );
        return nullptr;
    }

    if (ModuleName.IsEmpty())
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Cannot load a module with an empty name."
        );
        return nullptr;
    }

    if (FModuleRecord* Existing = LoadedModules.Find(ModuleName))
    {
        switch (Existing->State)
        {
            case EModuleState::Started:
                return Existing->Instance;

            case EModuleState::Loading:
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ModuleManager] Recursive load rejected for '{}'.",
                    ModuleName.CStr()
                );
                return nullptr;

            case EModuleState::Stopping:
            case EModuleState::Detaching:
            case EModuleState::Attaching:
                VENGINE_LOG(
                    LogCore,
                    Warning,
                    "[ModuleManager] Cannot load '{}' during a lifecycle transition.",
                    ModuleName.CStr()
                );
                return nullptr;

            case EModuleState::Failed:
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ModuleManager] Previous load of '{}' failed. "
                    "Unload it before attempting another load.",
                    ModuleName.CStr()
                );
                return nullptr;

            case EModuleState::Detached:
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ModuleManager] Reattachment of '{}' is not implemented yet.",
                    ModuleName.CStr()
                );
                return nullptr;
        }
    }

    void* Handle = FPlatformProcess::GetDllHandle(ModuleName);

    if (!Handle)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Failed to load shared library '{}'.",
            ModuleName.CStr()
        );
        return nullptr;
    }

    // Insert the record before calling runtime code. The module is already
    // mapped, so static initialization may have run, but the engine now has
    // a stable identity for the module.
    FModuleRecord NewRecord;
    NewRecord.Handle = Handle;
    NewRecord.Instance = nullptr;
    NewRecord.State = EModuleState::Loading;

    LoadedModules.Add(ModuleName, NewRecord);

    auto MarkFailed = [&](bool bStartupWasEntered)
    {
        if (FModuleRecord* Record = LoadedModules.Find(ModuleName))
        {
            Record->State = EModuleState::Failed;

            // If failure happened before StartupModule(), there is no
            // author startup state to shut down.
            if (!bStartupWasEntered)
            {
                Record->bShutdownExecuted = true;
            }
        }

        // Add only after the load attempt has finished. Nested dependencies
        // loaded during StartupModule() must precede this module in LoadOrder.
        if (!LoadOrder.Contains(ModuleName))
        {
            LoadOrder.Add(ModuleName);
        }
    };

    // Runtime state is established before module contributions and
    // StartupModule() are processed.
    if (!NotifyModuleLoaded(ModuleName))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime initialization failed for '{}'.",
            ModuleName.CStr()
        );

        MarkFailed(false);
        return nullptr;
    }

    auto CreateFunc = reinterpret_cast<CreateModuleFn>(
        FPlatformProcess::GetDllExport(Handle, "CreateModule")
    );

    if (!CreateFunc)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] CreateModule() export not found in '{}'.",
            ModuleName.CStr()
        );

        MarkFailed(false);
        return nullptr;
    }

    IModuleInterface* ModuleInstance = nullptr;

    try
    {
        ModuleInstance = CreateFunc();
    }
    catch (...)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] CreateModule() threw for '{}'.",
            ModuleName.CStr()
        );

        MarkFailed(false);
        return nullptr;
    }

    if (!ModuleInstance)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] CreateModule() returned null for '{}'.",
            ModuleName.CStr()
        );

        MarkFailed(false);
        return nullptr;
    }

    FModuleRecord* Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle)
    {
        // The module record must not disappear while its own load is active.
        // Do not free the DLL here: the module instance was created from it.
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ModuleManager] Module record disappeared during creation of '{}'.",
            ModuleName.CStr()
        );

        return nullptr;
    }

    Record->Instance = ModuleInstance;

    bool bStarted = false;

    try
    {
        // The wrapper performs:
        // PrepareModule()
        // FModuleClassRegistry::RegisterAll()
        // ModuleClass::StartupModule()
        bStarted = ModuleInstance->StartupModule();
    }
    catch (...)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] StartupModule() threw for '{}'.",
            ModuleName.CStr()
        );
    }

    Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle || Record->Instance != ModuleInstance)
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ModuleManager] Module record changed during startup of '{}'.",
            ModuleName.CStr()
        );

        return nullptr;
    }

    if (!bStarted)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Startup failed for '{}'. "
            "The DLL is retained for safe cleanup.",
            ModuleName.CStr()
        );

        MarkFailed(true);
        return nullptr;
    }

    Record->State = EModuleState::Started;

    if (!LoadOrder.Contains(ModuleName))
    {
        LoadOrder.Add(ModuleName);
    }

    VENGINE_LOG(
        LogCore,
        Info,
        "[ModuleManager] Loaded module '{}'.",
        ModuleName.CStr()
    );

    return ModuleInstance;
}

// ============================================================================
// Unloading
// ============================================================================

bool FModuleManager::UnloadModule(
    const FString& ModuleName,
    EModuleTeardownMode Mode)
{
    if (GIsDispatchingRuntimeCallbacks)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Module unloading rejected during a runtime callback."
        );
        return false;
    }

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

    if (InitialRecord->State == EModuleState::Loading)
    {
        VENGINE_LOG(
            LogCore,
            Warning,
            "[ModuleManager] Cannot unload '{}' while it is loading.",
            ModuleName.CStr()
        );
        return false;
    }

    if (InitialRecord->State == EModuleState::Detached ||
        InitialRecord->State == EModuleState::Attaching ||
        InitialRecord->State == EModuleState::Detaching)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Invalid unload transition for '{}'.",
            ModuleName.CStr()
        );
        return false;
    }

    // Detach is deliberately disabled until the module wrapper supports
    // registration -> rehydration -> author startup as separate phases.
    //
    // Preflight remains available so we can later enable it without changing
    // the ordinary Destroy lifecycle.
    if (Mode == EModuleTeardownMode::Detach)
    {
        if (!CanDetachModule(ModuleName))
        {
            VENGINE_LOG(
                LogCore,
                Warning,
                "[ModuleManager] Module '{}' rejected hot detach.",
                ModuleName.CStr()
            );
            return false;
        }

        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Hot detach is not enabled yet. "
            "The reattachment lifecycle is not implemented."
        );
        return false;
    }

    // A runtime implementation must outlive every module whose state it
    // manages. Runtime modules are foundational and must be unloaded last.
    if (HasRuntimeOwnedBy(ModuleName))
    {
        for (const FString& LoadedName : LoadOrder)
        {
            if (LoadedName != ModuleName &&
                LoadedModules.Find(LoadedName))
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ModuleManager] Cannot unload runtime owner '{}' while "
                    "module '{}' is still loaded.",
                    ModuleName.CStr(),
                    LoadedName.CStr()
                );
                return false;
            }
        }
    }

    InitialRecord->bUnloadInProgress = true;
    FUnloadGuard UnloadGuard(*this, ModuleName);

    // Copy values before invoking external module code. Never keep a pointer
    // into LoadedModules across a callback.
    IModuleInterface* Instance = InitialRecord->Instance;
    void* Handle = InitialRecord->Handle;

    if (!Handle)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Module '{}' has no valid DLL handle.",
            ModuleName.CStr()
        );
        return false;
    }

    if (InitialRecord->bShutdownFailed)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Cannot unload '{}': ShutdownModule() previously "
            "threw. The module is retained because its state is unknown.",
            ModuleName.CStr()
        );
        return false;
    }

    // ------------------------------------------------------------------------
    // Phase 1: author shutdown.
    // ------------------------------------------------------------------------

    FModuleRecord* Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle || Record->Instance != Instance)
    {
        return false;
    }

    Record->State = EModuleState::Stopping;

    if (Instance && !Record->bShutdownExecuted)
    {
        // Mark before calling external code. If it throws, we must not call
        // it again because its partial effects are unknown.
        Record->bShutdownExecuted = true;

        try
        {
            Instance->ShutdownModule();
        }
        catch (...)
        {
            Record = LoadedModules.Find(ModuleName);

            if (Record && Record->Instance == Instance)
            {
                Record->bShutdownFailed = true;
            }

            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] ShutdownModule() threw for '{}'. "
                "The DLL will remain loaded.",
                ModuleName.CStr()
            );

            return false;
        }
    }

    // ------------------------------------------------------------------------
    // Phase 2: runtime teardown.
    // ------------------------------------------------------------------------

    Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle || Record->Instance != Instance)
    {
        return false;
    }

    if (!NotifyModuleTeardown(ModuleName, EModuleTeardownMode::Destroy))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ModuleManager] Runtime teardown failed for '{}'. "
            "The DLL remains loaded and cleanup can be retried.",
            ModuleName.CStr()
        );

        return false;
    }

    // The owner's runtime must be removed after its teardown callback has
    // completed, but before the owner DLL is released.
    RemoveRuntimesOwnedBy(ModuleName);

    // ------------------------------------------------------------------------
    // Phase 3: contribution finalization.
    // ------------------------------------------------------------------------

    Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle || Record->Instance != Instance)
    {
        return false;
    }

    if (Instance && !Record->bFinalizationExecuted)
    {
        bool bFinalized = false;

        try
        {
            bFinalized = Instance->FinalizeModule();
        }
        catch (...)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] FinalizeModule() threw for '{}'.",
                ModuleName.CStr()
            );
        }

        if (!bFinalized)
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Finalization of '{}' did not complete. "
                "The DLL remains loaded for a later retry.",
                ModuleName.CStr()
            );

            return false;
        }

        Record = LoadedModules.Find(ModuleName);

        if (!Record || Record->Handle != Handle || Record->Instance != Instance)
        {
            return false;
        }

        Record->bFinalizationExecuted = true;
    }

    // ------------------------------------------------------------------------
    // Phase 4: destroy the module object.
    // ------------------------------------------------------------------------

    if (Instance)
    {
        // The DLL must still be mapped because the module's destructor and
        // deallocation function may reside in it.
        delete Instance;
    }

    Record = LoadedModules.Find(ModuleName);

    if (!Record || Record->Handle != Handle || Record->Instance != Instance)
    {
        VENGINE_LOG(
            LogCore,
            Fatal,
            "[ModuleManager] Module record changed during destruction of '{}'.",
            ModuleName.CStr()
        );

        return false;
    }

    // ------------------------------------------------------------------------
    // Phase 5: release the DLL.
    // ------------------------------------------------------------------------

    LoadedModules.Remove(ModuleName);
    LoadOrder.Remove(ModuleName);

    FPlatformProcess::FreeDllHandle(Handle);

    VENGINE_LOG(
        LogCore,
        Info,
        "[ModuleManager] Unloaded module '{}'.",
        ModuleName.CStr()
    );

    return true;
}

// ============================================================================
// Unload all
// ============================================================================

void FModuleManager::UnloadAllModules()
{
    // LoadOrder is dependency order: dependencies are loaded before dependents.
    // Therefore unload in reverse order.
    while (!LoadOrder.IsEmpty())
    {
        const FString LastModule = LoadOrder[LoadOrder.Num() - 1];

        if (!UnloadModule(LastModule, EModuleTeardownMode::Destroy))
        {
            VENGINE_LOG(
                LogCore,
                Error,
                "[ModuleManager] Aborting remaining unloads because '{}' "
                "could not be safely unloaded. Lower-level dependencies "
                "will remain loaded.",
                LastModule.CStr()
            );

            return;
        }
    }
}
