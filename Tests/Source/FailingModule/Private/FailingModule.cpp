#include "FailingModule.h"

#include "Core.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Object/ObjectRegistry.h"

#include <cstdlib>

class FFailingModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        // FModuleClassRegistry::RegisterAll() runs before this method.
        //
        // FFailingModValidPrefix should already have registered successfully.
        // FFailingModDuplicate then deliberately collides with the existing
        // "RObject" registration, causing the generated wrapper's startup
        // sequence to fail before reaching this method.
        //
        // This method is intentionally simple: the important test is that
        // the module manager retains the partially initialized DLL so its
        // registered prefix can be cleaned up safely.
        return true;
    }

    void ShutdownModule() override
    {
        FObjectRegistry& Registry = FObjectRegistry::Get();
        FModuleManager& Modules = FModuleManager::Get();

        const FString ModuleName = VENGINE_CURRENT_MODULE_NAME;

        // The module must still be in the manager while ShutdownModule runs.
        if (!Modules.IsModuleUnloading(ModuleName) ||
            Modules.IsModuleLoaded(ModuleName))
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[FailingModuleTest] FAILED: Module manager state is invalid "
                "during deferred shutdown."
            );
            std::abort();
        }

        // The successfully registered prefix must still exist at this point.
        const FClassInfo* PrefixInfo =
            Registry.FindClass("FFailingModValidPrefix");

        if (!PrefixInfo)
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[FailingModuleTest] FAILED: Valid prefix metadata was "
                "prematurely removed before ShutdownModule()."
            );
            std::abort();
        }

        if (PrefixInfo->OwningModule != ModuleName)
        {
            VENGINE_LOG(
                LogCore,
                Fatal,
                "[FailingModuleTest] FAILED: Prefix class has an unexpected "
                "owning module."
            );
            std::abort();
        }

        VENGINE_LOG(
            LogCore,
            Info,
            "[FailingModuleTest] PASSED: Partial registration survived "
            "until deferred shutdown."
        );
    }
};

IMPLEMENT_MODULE(FFailingModule)