#include "FailingModule.h"
#include "Core.h"
#include "Modules/ModuleInterface.h"

class FFailingModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        // Note: RegisterAll() runs after PrepareModule() and BEFORE StartupModule(),
        // so when FFailingModDuplicate collides with "RObject", StartupModule() is skipped
        // and the module enters Failed state for deferred shutdown.
        return true;
    }

    void ShutdownModule() override
    {
        // Verify that FFailingModValidPrefix metadata was preserved until ShutdownModule() runs!
        if (FObjectRegistry::Get().FindClass("FFailingModValidPrefix") == nullptr)
        {
            VENGINE_LOG(LogCore, Fatal, "[FailingModuleTest] FAILED: Valid prefix class metadata was prematurely destroyed before ShutdownModule()!");
            std::abort();
        }
        VENGINE_LOG(LogCore, Info, "[FailingModuleTest] PASSED: Valid prefix class metadata preserved through deferred shutdown.");
    }
};

IMPLEMENT_MODULE(FFailingModule)
