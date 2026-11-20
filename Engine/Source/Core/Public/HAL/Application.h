#pragma once
#include "CoreAPI.h"
#include "HAL/Window.h"
#include "Templates/SmartPointer.h"
#include "Containers/Array.h"

class Core_API FGenericApplication
{
    public:
    virtual ~FGenericApplication() = default;

    // Global active application backend (set by Launch / Platform / Renderer module)
    static void SetApplication(TSharedPtr<FGenericApplication> InApp);
    static FGenericApplication* Get();

    // Creates a platform window from FWindowDefinition
    virtual TSharedPtr<FGenericWindow> MakeWindow(const FWindowDefinition& Definition);

    // Pumps OS window & input events (must be called on the thread that owns the window)
    virtual void PumpMessages(float DeltaTime);

    // Returns true if a quit/exit signal was received from the OS
    bool IsExitRequested() const { return bExitRequested; }
    void RequestExit() { bExitRequested = true; }

    const TArray<TSharedPtr<FGenericWindow>>& GetWindows() const { return Windows; }

    protected:
    TArray<TSharedPtr<FGenericWindow>> Windows;
    bool bExitRequested = false;
};