#pragma once
#include "ApplicationAPI.h"
#include "Application/Window.h"
#include "Containers/Array.h"
#include "Templates/SmartPointer.h"
#include "HAL/Platform.h"

class FPlatformApplication;

// ==============================================================================
// The engine's application layer.
//
// Owns the platform backend and every open window, and defines the process-wide rules that
// are not OS-specific:
//
//   - the backend is created on first use, so an executable that never opens a window (the
//     test runner, a command-line tool) never initialises GLFW;
//   - closing a window requests process exit (single-window policy; revisit when multi-window
//     support arrives);
//   - presenting is explicit, so the loop can insert simulation and rendering between
//     PumpEvents() and Present() without this class knowing about either.
//
// There is exactly one instance per process, and it lives in this module. Game code talks to
// it through Get(); it never owns or constructs one.
//
// The loop drives it in this order:
//
//     App.PumpEvents();   // OS events -> window state; may set the exit request
//     ... engine/game frame ...
//     App.Present();      // publish every visible window
//
// Deliberately not here yet: an event queue (it arrives with keyboard/mouse, where order
// matters), a renderer (the backend clears until one exists), and input state.
// ==============================================================================
class Application_API FApplication
{
public:
    static FApplication& Get();

    FApplication(const FApplication&) = delete;
    FApplication& operator=(const FApplication&) = delete;
    FApplication(FApplication&&) = delete;
    FApplication& operator=(FApplication&&) = delete;

    // Opens a window. Returns nullptr (and logs) when the platform cannot provide one.
    FWindow* CreateWindow(const FWindowDefinition& Definition = FWindowDefinition());

    // Removes and closes a window. Ignores null, and refuses a pointer it does not own.
    // Destroying a window does not request exit: only closing one does.
    void DestroyWindow(FWindow* Window);

    // Drains OS events and updates exit state. No-op until the first window exists.
    void PumpEvents();

    // Presents every visible window.
    void Present();

    bool IsExitRequested() const { return bExitRequested; }

    // Same effect as closing a window, for code that decides the process should end.
    void RequestExit() { bExitRequested = true; }

    bool IsInitialized() const;

    int32 GetWindowCount() const { return Windows.Num(); }

    // Destroys every window and the platform backend. Called by the Application module on
    // unload; idempotent. The module's static FApplication is destroyed right after, which
    // calls this a second time as a no-op.
    void Shutdown();

private:
    FApplication();
    ~FApplication();

    bool EnsureInitialized();

    TUniquePtr<FPlatformApplication> PlatformApplication;
    TArray<FWindow*> Windows;
    bool bExitRequested = false;
};