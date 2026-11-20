#pragma once
#include "Modules/ModuleInterface.h"

// Private on purpose: nothing outside this module names this class. The generated
// TestsModule.gen.cpp only needs it to give IMPLEMENT_MODULE a complete type.
// Add overrides here and implement them in Tests.cpp.
class FTestsModule : public IModuleInterface
{
public:
    bool StartupModule() override;
    void ShutdownModule() override;
};
