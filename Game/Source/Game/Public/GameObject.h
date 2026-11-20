#pragma once
#include "Game.h"
#include "Core.h"
#include "Delegates/Delegate.h"
#include "GameFramework/WordObject.h"
#include "Object/Object.h"

class Game_API WGameObject : public WObject
{
public:
    RCLASS(WGameObject)
    
    DECLARE_EVENT_OneParam(WGameObject, FOnHealthChanged, float)
    FOnHealthChanged OnHealthChanged;
    WGameObject()
    {
        OnHealthChanged.Broadcast(Health);
        VENGINE_LOG( LogCore, Info, "Entity created!");
    }

    float Health = 10.0f;
    void Test()
    {
        VENGINE_LOG(LogCore, Info, "Game Entity Test");
        WGameObject* GEPtr = NewObject<WGameObject>(this);
        TArray<FString> Names = {"name 1","name 2","name 3"};
    };
};

