#pragma once
#include "ApplicationAPI.h"
#include "Containers/String.h"
#include "HAL/Platform.h"

class FPlatformApplication;
class FPlatformWindow;

// ==============================================================================
// Everything a caller may ask for when opening a window.
//
// Plain data: no behaviour, no platform types, no GLFW. The backend reads it once when the
// window is created and keeps its own copy, so mutating a definition afterwards does nothing.
// ==============================================================================
struct FWindowDefinition
{
    // UTF-8. The game decides its own title; the engine never guesses it.
    FString Title = "VEngine";

    // Window size in screen coordinates, not pixels. On a high-DPI display the framebuffer is
    // larger; that difference is exactly why FWindow exposes both sizes.
    int32 Width = 1280;
    int32 Height = 720;

    bool bResizable = true;
    bool bVisible = true;

    // Placeholder frame colour. Until a renderer module exists, the backend clears the window
    // to this colour so an otherwise empty window is visibly alive. It moves to a viewport or
    // camera when drawing is real, which is why it is not part of the general window state.
    float ClearColor[3] = { 0.05f, 0.06f, 0.08f };
};

// ==============================================================================
// An engine window: the handle game code holds.
//
// It owns no OS object itself. FApplication owns the backend; the backend owns the OS window.
// The pair is destroyed only through FApplication::DestroyWindow(), which is why the
// destructor is private: a stray delete would leave FApplication with a dangling entry and
// orphan the OS window.
//
// Everything that is per-window later (viewport, swap chain, input state, cursor) belongs
// here rather than in FApplication.
// ==============================================================================
class Application_API FWindow
{
public:
    FWindow(const FWindow&) = delete;
    FWindow& operator=(const FWindow&) = delete;
    FWindow(FWindow&&) = delete;
    FWindow& operator=(FWindow&&) = delete;

    // The definition the window was created with. Title follows SetTitle(); Width/Height are
    // what was requested, not the current size: use GetSize() for the live value.
    const FWindowDefinition& GetDefinition() const { return Definition; }

    // GLFWwindow* today; HWND / NSWindow* under another backend. Opaque on purpose: only the
    // renderer (and, later, input adapters) may look behind it.
    void* GetNativeHandle() const;

    void SetTitle(const FString& Title);

    // Window size in screen coordinates.
    void GetSize(int32& OutWidth, int32& OutHeight) const;

    // Drawable size in pixels. Differs from GetSize() under DPI scaling, and it is the size a
    // viewport or swap chain must use.
    void GetFramebufferSize(int32& OutWidth, int32& OutHeight) const;

    void Show();
    void Hide();
    bool IsVisible() const;

    // Publishes this window's back buffer. The loop calls it once per frame, after the frame's
    // simulation and UI work, so the window always shows the frame that was just produced.
    void Present();

    // True once the user (or the OS) asked to close the window. FApplication turns this into a
    // process exit request; callers only read it.
    bool ShouldClose() const;

    // Asks the OS to close this window, exactly as clicking its close button would. The frame
    // that is running finishes; FApplication then requests process exit.
    void RequestClose();

private:
    friend class FApplication;

    FWindow(FPlatformApplication* InOwner, FPlatformWindow* InPlatformWindow, const FWindowDefinition& InDefinition);
    ~FWindow();

    FPlatformApplication* Owner = nullptr;
    FPlatformWindow* PlatformWindow = nullptr;
    FWindowDefinition Definition;
};