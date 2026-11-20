#pragma once
#include "CoreAPI.h"
#include "Containers/String.h"
#include "Delegates/Delegate.h"
#include <cstdint>

struct FWindowDefinition
{
    FString Title = "VEngine";
    int32_t Width = 1280;
    int32_t Height = 720;
    bool bResizable = true;
    bool bFullscreen = false;
    bool bVSync = true;
};

class Core_API FGenericWindow
{
public:
    virtual ~FGenericWindow() = default;

    // Lifecycle & Properties
    virtual void Show() {}
    virtual void Hide() {}
    virtual void Destroy() {}
    virtual void Resize(int32_t NewWidth, int32_t NewHeight) { Width = NewWidth; Height = NewHeight; }

    virtual bool IsValid() const { return false; }
    virtual bool ShouldClose() const { return bShouldClose; }
    virtual void* GetNativeHandle() const { return nullptr; }

    int32_t GetWidth() const { return Width; }
    int32_t GetHeight() const { return Height; }
    const FString& GetTitle() const { return Title; }

    // Owner-restricted OS events (only FGenericWindow subclasses can Broadcast)
    DECLARE_EVENT_TwoParams(FGenericWindow, FOnWindowResizedEvent, int32_t /*NewWidth*/, int32_t /*NewHeight*/)
    DECLARE_EVENT(FGenericWindow, FOnWindowClosedEvent)
    DECLARE_EVENT_TwoParams(FGenericWindow, FOnKeyEvent, int32_t /*KeyCode*/, bool /*bIsRepeat*/)
    DECLARE_EVENT_OneParam(FGenericWindow, FOnKeyReleasedEvent, int32_t /*KeyCode*/)
    DECLARE_EVENT_TwoParams(FGenericWindow, FOnMouseMovedEvent, float /*X*/, float /*Y*/)
    DECLARE_EVENT_OneParam(FGenericWindow, FOnMouseButtonEvent, int32_t /*Button*/)

    FOnWindowResizedEvent OnWindowResized;
    FOnWindowClosedEvent OnWindowClosed;
    FOnKeyEvent OnKeyDown;
    FOnKeyReleasedEvent OnKeyUp;
    FOnMouseMovedEvent OnMouseMoved;
    FOnMouseButtonEvent OnMouseButtonDown;
    FOnMouseButtonEvent OnMouseButtonUp;

protected:
    void NotifyResized(int32_t NewWidth, int32_t NewHeight)
    {
        Width = NewWidth;
        Height = NewHeight;
        OnWindowResized.Broadcast(NewWidth, NewHeight);
    }

    void NotifyClosed()
    {
        bShouldClose = true;
        OnWindowClosed.Broadcast();
    }

    void NotifyKeyDown(int32_t KeyCode, bool bIsRepeat = false) { OnKeyDown.Broadcast(KeyCode, bIsRepeat); }
    void NotifyKeyUp(int32_t KeyCode) { OnKeyUp.Broadcast(KeyCode); }
    void NotifyMouseMoved(float X, float Y) { OnMouseMoved.Broadcast(X, Y); }
    void NotifyMouseButtonDown(int32_t Button) { OnMouseButtonDown.Broadcast(Button); }
    void NotifyMouseButtonUp(int32_t Button) { OnMouseButtonUp.Broadcast(Button); }

    FString Title = "VEngine";
    int32_t Width = 1280;
    int32_t Height = 720;
    bool bShouldClose = false;
};