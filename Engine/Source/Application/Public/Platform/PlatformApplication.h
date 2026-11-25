#pragma once
#include "ApplicationAPI.h"

struct FWindowDefinition;
class FPlatformWindow;

// ==============================================================================
// The OS layer behind FApplication: one instance per process, owned by the module.
//
// Exactly one backend is compiled into the Application module, and it is selected in exactly
// one place: Private/Platform/PlatformApplication.cpp. Writing a native Win32 or Cocoa
// backend means implementing this interface plus FPlatformWindow and returning it from
// CreateGLFWApplication()'s replacement there. Nothing above this line changes: FApplication,
// FWindow, the loop and game code stay identical.
//
// The interface is public only so a backend can move to its own module later. Game code never
// touches it; game code uses FApplication and FWindow.
//
// Ownership: FApplication owns the platform application, the platform application owns its
// windows, and each window is destroyed through the DestroyWindow() of the same backend that
// created it.
// ==============================================================================
class Application_API FPlatformApplication
{
public:
    virtual ~FPlatformApplication() = default;

    // Initialises the OS layer. Called by FApplication before the first window is created;
    // returns false when the platform cannot be initialised (for example, no desktop session),
    // and the caller logs and reports the failure instead of aborting the process.
    virtual bool Initialize() = 0;

    // Terminates the OS layer. Every window must already have been destroyed through
    // DestroyWindow(): the backend cannot outlive the windows it created.
    virtual void Shutdown() = 0;

    // Drains pending OS events. Must run on the thread that owns the windows.
    virtual void PumpMessages() = 0;

    // Returns nullptr (having logged) when the OS refuses to create the window.
    virtual FPlatformWindow* CreateWindow(const FWindowDefinition& Definition) = 0;

    // Destroys a window created by this backend. Ignores null.
    virtual void DestroyWindow(FPlatformWindow* Window) = 0;
};