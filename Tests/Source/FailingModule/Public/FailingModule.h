#pragma once
#include "FailingModuleAPI.h"
#include "Object/Object.h"

class FailingModule_API FFailingModValidPrefix : public RObject
{
    RCLASS(FFailingModValidPrefix)
public:
    FFailingModValidPrefix() = default;
};

// Deliberately collides with "RObject" during automatic module registration
class FailingModule_API FFailingModDuplicate : public RObject
{
    public:
    using Super = RObject;
    using ThisClass = FFailingModDuplicate;
    static const char* StaticClassName() { return "RObject"; }

    private:
    inline static TObjectRegistrar<FFailingModDuplicate> AutoRegistrar{};
};