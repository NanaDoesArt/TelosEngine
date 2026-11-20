#include "HAL/Application.h"
#include "Logging/Log.h"

namespace { TSharedPtr<FGenericApplication> ActiveApplication = nullptr; }

void FGenericApplication::SetApplication(TSharedPtr<FGenericApplication> InApp)
{
    ActiveApplication = eastl::move(InApp);
    if (ActiveApplication)
    {
        VENGINE_LOG(LogCore, Info, "[Application] Platform application backend initialized.");
    }
}

FGenericApplication* FGenericApplication::Get()
{
    return ActiveApplication.Get();
}

TSharedPtr<FGenericWindow> FGenericApplication::MakeWindow(const FWindowDefinition& Definition)
{
    TSharedPtr<FGenericWindow> NewWindow = MakeShared<FGenericWindow>();
    Windows.Add(NewWindow);
    return NewWindow;
}

void FGenericApplication::PumpMessages(float /*DeltaTime*/)
{
    // Base implementation checks if all windows have requested close
    for (int32_t i = Windows.Num() - 1; i >= 0; --i)
    {
        if (Windows[i]->ShouldClose())
        {
            Windows[i]->Destroy();
            Windows.RemoveAt(i);
        }
    }

    if (Windows.IsEmpty())
    {
        bExitRequested = true;
    }
}