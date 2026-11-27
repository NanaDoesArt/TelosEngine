#include "ObjectRuntime.h"
#include "Object/ObjectRegistry.h"
#include "Modules/ModuleManager.h"
#include "Logging/Log.h"

namespace
{
    // ==========================================================================
    // The object runtime.
    //
    // This is the only place where the module lifecycle meets object lifetime. Core sees
    // an IModuleRuntime and two strings; it never sees RObject, FObjectRegistry or a scope.
    //
    // The runtime is a function-local static in this module. The manager removes the
    // registration before this module's library is freed, and never deletes the runtime.
    // ==========================================================================
    class FObjectRuntime final : public IModuleRuntime
    {
    public:
        void SetOwner(const FString& InOwner)
        {
            if (Owner.IsEmpty())
            {
                Owner = InOwner;
            }
        }

        const FString& GetOwner() const
        {
            return Owner;
        }

        bool OnModuleLoaded(const FString& ModuleName) override
        {
            // One root per module, created before the module's own contributions and
            // StartupModule run. Static initialization before the runtime existed is covered
            // by the same lazy path inside NewObject<T>().
            if (!FObjectRegistry::Get().GetOrCreateModuleScope(ModuleName))
            {
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ObjectRuntime] Failed to create the lifetime scope for '{}'.",
                    ModuleName.CStr()
                );
                return false;
            }

            return true;
        }

        bool CanDetachModule(const FString& ModuleName) override
        {
            // A native instance, its vtable, its destructor and its interface thunks are code
            // in the module's library. Keeping them alive after FreeDllHandle() would be a
            // use-after-unload. Until the runtime captures every reflected value into
            // engine-owned records, no module may detach.
            if (!bWarnedAboutDetach)
            {
                bWarnedAboutDetach = true;
                VENGINE_LOG(
                    LogCore,
                    Warning,
                    "[ObjectRuntime] Hot detach is disabled: property capture is not implemented, "
                    "so '{}' must stay loaded.",
                    ModuleName.CStr()
                );
            }

            return false;
        }

        bool OnModuleTeardown(const FString& ModuleName, EModuleTeardownMode Mode) override
        {
            if (Mode == EModuleTeardownMode::Detach)
            {
                // Unreachable while CanDetachModule() returns false. Kept as a hard stop so a
                // future caller cannot free a library whose state was never captured.
                VENGINE_LOG(
                    LogCore,
                    Error,
                    "[ObjectRuntime] Refusing to detach '{}' at teardown: capture support is missing.",
                    ModuleName.CStr()
                );
                return false;
            }

            FObjectRegistry& Registry = FObjectRegistry::Get();

            // Destroys the module's lifetime scope and refuses the unload if objects of this
            // module's classes survive in another module's scope.
            if (!Registry.ReleaseModuleObjects(ModuleName))
            {
                return false;
            }

            if (ModuleName == Owner)
            {
                // Every module that can create objects depends on this module, so all of them
                // have unloaded by now. Only Core remains, and it owns no objects.
                Registry.Shutdown();
            }

            return true;
        }

        bool OnModuleReattached(const FString& ModuleName) override
        {
            // Unreachable while CanDetachModule() returns false.
            return true;
        }

    private:
        FString Owner;
        bool bWarnedAboutDetach = false;
    };

    FObjectRuntime& GetRuntime()
    {
        static FObjectRuntime Runtime;
        return Runtime;
    }
}

bool RegisterObjectRuntime(const char* OwnerModule)
{
    const FString Owner(OwnerModule ? OwnerModule : "UnknownModule");

    FObjectRuntime& Runtime = GetRuntime();
    Runtime.SetOwner(Owner);

    if (Runtime.GetOwner() != Owner)
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRuntime] Already owned by '{}'; refusing a second registration from '{}'.",
            Runtime.GetOwner().CStr(),
            Owner.CStr()
        );
        return false;
    }

    // The runtime's own module gets a scope too, so "one root per module" is literally true.
    if (!FObjectRegistry::Get().GetOrCreateModuleScope(Owner))
    {
        return false;
    }

    if (!FModuleManager::Get().RegisterRuntime(Owner, &Runtime))
    {
        VENGINE_LOG(
            LogCore,
            Error,
            "[ObjectRuntime] Failed to register the object runtime."
        );

        FObjectRegistry::Get().ReleaseModuleObjects(Owner);
        return false;
    }
    return true;
}