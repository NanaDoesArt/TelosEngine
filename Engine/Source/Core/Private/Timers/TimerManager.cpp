#include "Timers/TimerManager.h"

void FTimerManager::Tick(float DeltaTime)
{
    if (Timers.IsEmpty())
    {
        return;
    }

    bIsTicking = true;

    // Collect active handle keys first so callbacks can safely add or clear timers mid-tick
    TArray<FTimerHandle> ActiveKeys;
    ActiveKeys.Reserve(Timers.Num());
    for (const auto& Pair : Timers)
    {
        ActiveKeys.Add(Pair.first);
    }

    for (const FTimerHandle& Handle : ActiveKeys)
    {
        FTimerData* Data = Timers.Find(Handle);
        if (!Data || Data->bPendingRemoval || Data->bPaused)
        {
            continue;
        }

        Data->RemainingTime -= DeltaTime;
        if (Data->RemainingTime <= 0.0f)
        {
            // Copy delegate locally in case the callback clears this timer handle inside ExecuteIfBound
            FTimerDelegate CallbackCopy = Data->Delegate;

            if (Data->bLoop && Data->Rate > 0.0f)
            {
                // Preserve sub-frame accuracy on repeating timers
                while (Data->RemainingTime <= 0.0f)
                {
                    Data->RemainingTime += Data->Rate;
                }
            }
            else
            {
                Data->bPendingRemoval = true;
            }

            CallbackCopy.ExecuteIfBound();
        }
    }

    bIsTicking = false;

    // Sweep expired or cleared timers after iteration finishes
    TArray<FTimerHandle> ExpiredKeys;
    for (const auto& Pair : Timers)
    {
        if (Pair.second.bPendingRemoval)
        {
            ExpiredKeys.Add(Pair.first);
        }
    }

    for (const FTimerHandle& DeadHandle : ExpiredKeys)
    {
        Timers.Remove(DeadHandle);
    }
}

void FTimerManager::SetTimer(FTimerHandle& InOutHandle, FTimerDelegate InDelegate, float Rate, bool bLoop, float FirstDelay)
{
    // If reusing an existing handle, clear its old entry first
    if (InOutHandle.IsValid())
    {
        ClearTimer(InOutHandle);
    }

    if (Rate <= 0.0f && FirstDelay <= 0.0f)
    {
        return;
    }

    InOutHandle.ID = NextHandleID++;

    FTimerData NewTimer;
    NewTimer.Delegate = eastl::move(InDelegate);
    NewTimer.Rate = Rate;
    NewTimer.RemainingTime = (FirstDelay >= 0.0f) ? FirstDelay : Rate;
    NewTimer.bLoop = bLoop;
    NewTimer.bPaused = false;
    NewTimer.bPendingRemoval = false;

    Timers.Add(InOutHandle, eastl::move(NewTimer));
}

void FTimerManager::ClearTimer(FTimerHandle& InOutHandle)
{
    if (!InOutHandle.IsValid())
    {
        return;
    }

    if (FTimerData* Data = Timers.Find(InOutHandle))
    {
        if (bIsTicking)
        {
            Data->bPendingRemoval = true;
            Data->Delegate.Unbind();
        }
        else
        {
            Timers.Remove(InOutHandle);
        }
    }

    InOutHandle.Invalidate();
}

void FTimerManager::ClearAllTimers()
{
    if (bIsTicking)
    {
        for (auto& Pair : Timers)
        {
            Pair.second.bPendingRemoval = true;
            Pair.second.Delegate.Unbind();
        }
    }
    else
    {
        Timers.Empty();
    }
}

void FTimerManager::PauseTimer(const FTimerHandle& Handle)
{
    if (FTimerData* Data = Timers.Find(Handle))
    {
        if (!Data->bPendingRemoval)
        {
            Data->bPaused = true;
        }
    }
}

void FTimerManager::UnpauseTimer(const FTimerHandle& Handle)
{
    if (FTimerData* Data = Timers.Find(Handle))
    {
        if (!Data->bPendingRemoval)
        {
            Data->bPaused = false;
        }
    }
}

bool FTimerManager::IsTimerActive(const FTimerHandle& Handle) const
{
    const FTimerData* Data = Timers.Find(Handle);
    return Data && !Data->bPendingRemoval && !Data->bPaused;
}

bool FTimerManager::IsTimerPaused(const FTimerHandle& Handle) const
{
    const FTimerData* Data = Timers.Find(Handle);
    return Data && !Data->bPendingRemoval && Data->bPaused;
}

float FTimerManager::GetTimerRemaining(const FTimerHandle& Handle) const
{
    const FTimerData* Data = Timers.Find(Handle);
    return (Data && !Data->bPendingRemoval) ? Data->RemainingTime : -1.0f;
}

float FTimerManager::GetTimerElapsed(const FTimerHandle& Handle) const
{
    const FTimerData* Data = Timers.Find(Handle);
    return (Data && !Data->bPendingRemoval) ? (Data->Rate - Data->RemainingTime) : -1.0f;
}