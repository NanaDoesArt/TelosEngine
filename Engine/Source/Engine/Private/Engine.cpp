#include "Engine.h"
#include "Core.h"
#include "Modules/ModuleInterface.h"

class FEngineModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Engine] Module started!");
        return true;
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Engine] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FEngineModule)
