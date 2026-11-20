#pragma once
#include "EngineAPI.h"
#include "Object/Object.h"


class Engine_API WObject : public RObject
{
    RCLASS(WObject)
    public:
    virtual void BeginPlay() {}
    virtual void Tick(float DeltaTime) {}
};
