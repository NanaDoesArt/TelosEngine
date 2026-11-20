#include "Editor.h"
#include "Core.h"
#include "Modules/ModuleInterface.h"

class FEditorModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Editor_Editor] Module started!");
        return true;
    }

    void ShutdownModule() override
    {
        VENGINE_LOG(LogCore, Info, "[Editor_Editor] Module shutting down!");
    }
};

IMPLEMENT_MODULE(FEditorModule)
