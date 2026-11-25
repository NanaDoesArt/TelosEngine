#include "Application/Application.h"
#include "Platform/PlatformApplication.h"
#include "Platform/PlatformBackend.h"
#include "Logging/Log.h"

FApplication& FApplication::Get()
{
    // Function-local static: constructed on first use, destroyed when this DLL unloads.
    // Shutdown() runs first (module teardown), so that destruction is a no-op.
    static FApplication Instance;
    return Instance;
}

FApplication::FApplication() = default;

FApplication::~FApplication()
{
    Shutdown();
}

bool FApplication::IsInitialized() const
{
    return PlatformApplication.IsValid();
}

bool FApplication::EnsureInitialized()
{
    if (PlatformApplication.IsValid())
    {
        return true;
    }

    PlatformApplication.Reset(CreatePlatformApplication());
    if (!PlatformApplication.IsValid())
    {
        VENGINE_LOG(LogCore, Error, "[Application] The platform backend could not be created.");
        return false;
    }

    if (!PlatformApplication->Initialize())
    {
        VENGINE_LOG(LogCore, Error, "[Application] The platform backend failed to initialise.");
        PlatformApplication.Reset();
        return false;
    }

    return true;
}

FWindow* FApplication::CreateWindow(const FWindowDefinition& Definition)
{
    if (!EnsureInitialized())
    {
        return nullptr;
    }

    FPlatformWindow* PlatformWindow = PlatformApplication->CreateWindow(Definition);
    if (!PlatformWindow)
    {
        return nullptr;
    }

    FWindow* Window = new FWindow(this, PlatformWindow, Definition);
    Windows.Add(Window);

    VENGINE_LOG(
        LogCore,
        Info,
        "[Application] Opened window '{}' ({}x{}).",
        Definition.Title.CStr(),
        Definition.Width,
        Definition.Height
    );

    return Window;
}

void FApplication::DestroyWindow(FWindow* Window)
{
    if (!Window)
    {
        return;
    }

    // Remove first: it makes the window unreachable from Present()/PumpEvents() while its
    // destructor tears the OS object down, and it is also how a foreign pointer is caught
    // before it can be deleted.
    if (!Windows.Remove(Window))
    {
        VENGINE_LOG(
            LogCore,
            Warning,
            "[Application] DestroyWindow() was given a window this application does not own; ignoring it."
        );
        return;
    }

    delete Window;
}

void FApplication::PumpEvents()
{
    if (!PlatformApplication.IsValid())
    {
        return;
    }

    PlatformApplication->PumpMessages();

    // Single-window exit policy: closing a window ends the process. Multi-window support will
    // replace this with a per-window decision, which is why the policy lives here and not in
    // the backend.
    for (FWindow* Window : Windows)
    {
        if (Window->ShouldClose())
        {
            bExitRequested = true;
            break;
        }
    }
}

void FApplication::Present()
{
    for (FWindow* Window : Windows)
    {
        if (Window->IsVisible())
        {
            Window->Present();
        }
    }
}

void FApplication::Shutdown()
{
    // Windows first: the backend must not terminate the platform while they exist. Deleting
    // through the array (rather than through DestroyWindow()) avoids mutating it mid-loop;
    // each window's destructor still asks the backend to destroy its OS object.
    for (FWindow* Window : Windows)
    {
        delete Window;
    }
    Windows.Empty();

    if (PlatformApplication.IsValid())
    {
        PlatformApplication->Shutdown();
        PlatformApplication.Reset();
    }

    bExitRequested = false;
}