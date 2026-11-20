#pragma once
#include "Core.h"
#include <cstdint>

#include "Delegates/Delegate.h"

DECLARE_DELEGATE(FTimerDelegate)

struct FTimerHandle
{
    uint64_t ID = 0;

    bool IsValid() const { return ID != 0; }
    void Invalidate() { ID = 0; }

    bool operator==(const FTimerHandle& Other) const { return ID == Other.ID; }
    bool operator!=(const FTimerHandle& Other) const { return ID != Other.ID; }
};

namespace eastl
{
    template <>
    struct hash<FTimerHandle>
    {
        size_t operator()(const FTimerHandle& Handle) const
        {
            return static_cast<size_t>(Handle.ID);
        }
    };
}

class Core_API FTimerManager
{
public:
    FTimerManager() = default;

    // Advances all active timers by DeltaTime (call once per frame in EngineLoop::Tick)
    void Tick(float DeltaTime);

    // Schedules a timer using a pre-bound FTimerDelegate
    void SetTimer(FTimerHandle& InOutHandle, FTimerDelegate InDelegate, float Rate, bool bLoop = false, float FirstDelay = -1.0f);

    // Convenience overload: bind a lambda directly
    template <typename Functor>
    void SetTimer(FTimerHandle& InOutHandle, Functor&& InFunctor, float Rate, bool bLoop = false, float FirstDelay = -1.0f)
    {
        FTimerDelegate Del;
        Del.BindLambda(eastl::forward<Functor>(InFunctor));
        SetTimer(InOutHandle, eastl::move(Del), Rate, bLoop, FirstDelay);
    }

    // Convenience overload: bind a raw object member function
    template <typename UserClass>
    void SetTimer(FTimerHandle& InOutHandle, UserClass* InObject, void (UserClass::*InMethod)(), float Rate, bool bLoop = false, float FirstDelay = -1.0f)
    {
        FTimerDelegate Del;
        Del.BindRaw(InObject, InMethod);
        SetTimer(InOutHandle, eastl::move(Del), Rate, bLoop, FirstDelay);
    }

    void ClearTimer(FTimerHandle& InOutHandle);
    void ClearAllTimers();

    void PauseTimer(const FTimerHandle& Handle);
    void UnpauseTimer(const FTimerHandle& Handle);

    bool IsTimerActive(const FTimerHandle& Handle) const;
    bool IsTimerPaused(const FTimerHandle& Handle) const;
    float GetTimerRemaining(const FTimerHandle& Handle) const;
    float GetTimerElapsed(const FTimerHandle& Handle) const;

private:
    struct FTimerData
    {
        FTimerDelegate Delegate;
        float Rate = 0.0f;
        float RemainingTime = 0.0f;
        bool bLoop = false;
        bool bPaused = false;
        bool bPendingRemoval = false;
    };

    TMap<FTimerHandle, FTimerData> Timers;
    uint64_t NextHandleID = 1;
    bool bIsTicking = false;
};