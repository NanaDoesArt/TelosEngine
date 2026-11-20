#pragma once

#include "Object/Interface.h"

class IInterfaceLifetimeProbe
{
public:
    RINTERFACE(IInterfaceLifetimeProbe)

    virtual ~IInterfaceLifetimeProbe() = default;
    virtual int32_t GetProbeValue() const = 0;
};