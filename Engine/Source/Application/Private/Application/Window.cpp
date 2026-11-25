#include "Application/Window.h"
#include "Platform/PlatformApplication.h"
#include "Platform/PlatformWindow.h"

FWindow::FWindow(FPlatformApplication* InOwner, FPlatformWindow* InPlatformWindow, const FWindowDefinition& InDefinition)
    : Owner(InOwner)
    , PlatformWindow(InPlatformWindow)
    , Definition(InDefinition)
{
}

FWindow::~FWindow()
{
    if (Owner && PlatformWindow)
    {
        Owner->DestroyWindow(PlatformWindow);
    }

    PlatformWindow = nullptr;
}

void* FWindow::GetNativeHandle() const
{
    return PlatformWindow ? PlatformWindow->GetNativeHandle() : nullptr;
}

void FWindow::SetTitle(const FString& Title)
{
    if (!PlatformWindow)
    {
        return;
    }

    PlatformWindow->SetTitle(Title.CStr());
    Definition.Title = Title;
}

void FWindow::GetSize(int32& OutWidth, int32& OutHeight) const
{
    OutWidth = 0;
    OutHeight = 0;

    if (PlatformWindow)
    {
        PlatformWindow->GetSize(OutWidth, OutHeight);
    }
}

void FWindow::GetFramebufferSize(int32& OutWidth, int32& OutHeight) const
{
    OutWidth = 0;
    OutHeight = 0;

    if (PlatformWindow)
    {
        PlatformWindow->GetFramebufferSize(OutWidth, OutHeight);
    }
}

void FWindow::Show()
{
    if (PlatformWindow)
    {
        PlatformWindow->Show();
    }
}

void FWindow::Hide()
{
    if (PlatformWindow)
    {
        PlatformWindow->Hide();
    }
}

bool FWindow::IsVisible() const
{
    return PlatformWindow && PlatformWindow->IsVisible();
}

void FWindow::Present()
{
    if (PlatformWindow)
    {
        PlatformWindow->Present();
    }
}

bool FWindow::ShouldClose() const
{
    return PlatformWindow ? PlatformWindow->ShouldClose() : true;
}

void FWindow::RequestClose()
{
    if (PlatformWindow)
    {
        PlatformWindow->RequestClose();
    }
}