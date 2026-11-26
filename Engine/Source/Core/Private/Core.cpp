// The Core module is a normal module in every respect. It owns the allocator, containers,
// logging and the module loader; it does not know that objects exist.
//
// The object runtime used to be initialized here. It now lives in Engine::Object, which is a
// client of the module manager and registers an IModuleRuntime in its own PrepareModule.
#include "Core.h"

class FCoreModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Core] Module started!");
        return true;
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Engine_Core] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FCoreModule)