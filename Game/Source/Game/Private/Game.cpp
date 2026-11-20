#include "Game.h"
#include "Core.h"
#include "Modules/ModuleInterface.h"

class FGameModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Game_Game] Module started!");
        return true;
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Game_Game] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FGameModule)
