#pragma once
#include "ApplicationAPI.h"
#include "HAL/Platform.h"

// ==============================================================================
// The OS window behind FWindow.
//
// This is the only platform-specific part of a window, and it is deliberately small: every
// method here has to be implemented by any backend (GLFW today, native Win32 or Cocoa later).
// Anything that is not an OS operation (the window registry, exit policy, frame pacing)
// belongs to FApplication, not here.
//
// Ownership: FPlatformApplication::CreateWindow() returns one of these, and only the same
// backend may destroy it (FPlatformApplication::DestroyWindow()).
// ==============================================================================
class Application_API FPlatformWindow
{
public:
    virtual ~FPlatformWindow() = default;

    // GLFWwindow* / HWND / NSWindow*, depending on the backend. Opaque on purpose: only code
    // that knows the backend may use it.
    virtual void* GetNativeHandle() const = 0;

    virtual void SetTitle(const char* Title) = 0;

    // Window size in screen coordinates: what the title bar and window layout use.
    virtual void GetSize(int32& OutWidth, int32& OutHeight) const = 0;

    // Drawable size in pixels. Differs from GetSize() under DPI scaling, and it is the size a
    // viewport or swap chain must use.
    virtual void GetFramebufferSize(int32& OutWidth, int32& OutHeight) const = 0;

    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual bool IsVisible() const = 0;

    // Makes this window's context current on the calling thread.
    virtual void MakeContextCurrent() = 0;

    // Publishes the window's back buffer. Today the GLFW backend also clears to the window's
    // placeholder colour because there is no renderer yet; once one exists, drawing happens
    // before this call and Present() becomes a pure buffer swap.
    virtual void Present() = 0;

    virtual bool ShouldClose() const = 0;
    virtual void RequestClose() = 0;
};