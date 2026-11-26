// ==============================================================================
// MODULE BOILERPLATE - generated once by Templates/ModuleTemplate.cmake.
//
// This is where a module's lifecycle overrides live. The Object module is the one module
// in the tree whose PrepareModule is not empty: it installs the engine's object runtime,
// which is what makes every other module's objects possible.
// ==============================================================================
#include "RObject.h"
#include "Core.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "ObjectRuntime.h"

class FRObjectModule : public IModuleInterface
{
public:
    bool PrepareModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_RObject] Preparing object runtime...");

        // Installs the runtime and creates this module's own lifetime scope. The registry
        // registers its bootstrap classes on first use; nothing here has to be kept in sync.
        return RegisterObjectRuntime(VENGINE_CURRENT_MODULE_NAME);
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_RObject] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FRObjectModule)