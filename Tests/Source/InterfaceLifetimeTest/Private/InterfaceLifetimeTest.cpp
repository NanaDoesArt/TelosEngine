
#include "Object/Object.h"
#include "InterfaceTest.h"

class FInterfaceLifetimeProbe
    : public RObject
    , public IInterfaceLifetimeProbe
{
    RCLASS(FInterfaceLifetimeProbe)
    RIMPLEMENTS(IInterfaceLifetimeProbe)

public:
    int32_t GetProbeValue() const override
    {
        return 0x1234;
    }
};

class FInterfaceLifetimeModule : public IModuleInterface
{
public:
    bool StartupModule() override
    {
        return true;
    }
};

IMPLEMENT_MODULE(FInterfaceLifetimeModule)