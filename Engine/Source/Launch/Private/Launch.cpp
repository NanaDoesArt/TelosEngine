#include "Launch.h"
#include "Core.h"
#include "Modules/ModuleInterface.h"

class FLaunchModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Launch] Module started!");
        return true;
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Launch] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FLaunchModule)
