#include "LaunchEngineLoop.h"
#include "Core.h"
#include "HAL/Application.h"
#include "HAL/PlatformProcess.h"
#include "Async/ThreadPool.h"
#include "Logging/ConsoleDashboard.h"
#include "Modules/ModuleManager.h"
#include "Object/ObjectRegistry.h"

#include <algorithm>
#include <format>
#include <string>

FEngineLoop GEngineLoop;

using GetBootModulesFn = const char* const* (*)(int*);

int FEngineLoop::PreInit(int, char*[])
{
    // A second PreInit (embedding, in-process tests) starts a fresh run, not a continuation.
    bExitRequested.store(false, std::memory_order_relaxed);
    FrameNumber = 0;
    LastTickTimeSeconds = 0.0;
    SmoothedDeltaSeconds = 0.0;
    WorstDeltaSeconds = 0.0;
    WorstWindowStartSeconds = 0.0;
    WorstReportedSeconds = 0.0;
    FrameHistoryCount = 0;
    FrameHistoryNext = 0;
    FrameHistoryMilliseconds.fill(0.0);

    VENGINE_LOG(LogCore, Info, "[Launch] Engine PreInit...");
    FObjectRegistry::Get().BindGameThread();
    FPlatformTime::InitTiming();
    FThreadPool::Get().Init();
    return 0;
}

int FEngineLoop::Init()
{
    VENGINE_LOG(LogCore, Info, "[Launch] Engine Init: Loading boot modules...");

    auto GetModulesFunc = reinterpret_cast<GetBootModulesFn>(
        FPlatformProcess::GetMainExecutableExport("GetBootModules")
    );

    if (!GetModulesFunc)
    {
        VENGINE_LOG(LogCore, Error, "[Launch] Fatal: GetBootModules() not found in host executable.");
        return 1;
    }

    int ModuleCount = 0;
    const char* const* Modules = GetModulesFunc(&ModuleCount);

    for (int Index = 0; Index < ModuleCount; ++Index)
    {
        if (!FModuleManager::Get().LoadModule(Modules[Index]))
        {
            VENGINE_LOG(LogCore, Error, "[Launch] Fatal: Required boot module '{}' failed to load. Aborting startup.", Modules[Index]);
            // Return error directly; EngineMain() calls Exit() which drains FThreadPool before unloading modules
            return 2;
        }
    }

    StartTimeSeconds = FPlatformTime::Seconds();
    LastTickTimeSeconds = 0.0;
    WorstWindowStartSeconds = StartTimeSeconds;

    const std::string CapText = TargetFrameRate > 0.0
        ? std::format("{:.0f} FPS", TargetFrameRate)
        : std::string("uncapped");

    VENGINE_LOG(LogCore, Info, "[Launch] Main loop started: {}. Press Ctrl+C to stop.", CapText);

    return 0;
}

void FEngineLoop::Tick()
{
    const double FrameStart = FPlatformTime::Seconds();

    double DeltaSeconds = 0.0;
    if (LastTickTimeSeconds > 0.0)
    {
        DeltaSeconds = FrameStart - LastTickTimeSeconds;
    }
    LastTickTimeSeconds = FrameStart;

    if (DeltaSeconds > MaxDeltaSeconds)
    {
        DeltaSeconds = MaxDeltaSeconds;
    }

    ++FrameNumber;

    if (DeltaSeconds > 0.0)
    {
        SmoothedDeltaSeconds = (SmoothedDeltaSeconds <= 0.0)
            ? DeltaSeconds
            : SmoothedDeltaSeconds + (DeltaSeconds - SmoothedDeltaSeconds) * DeltaSmoothing;
    }

    WorstDeltaSeconds = std::max(WorstDeltaSeconds, DeltaSeconds);
    if (FrameStart - WorstWindowStartSeconds >= 1.0)
    {
        WorstReportedSeconds = WorstDeltaSeconds;
        WorstDeltaSeconds = 0.0;
        WorstWindowStartSeconds = FrameStart;
    }

    FrameHistoryMilliseconds[FrameHistoryNext] = DeltaSeconds * 1000.0;
    FrameHistoryNext = (FrameHistoryNext + 1) % FrameHistorySize;
    if (FrameHistoryCount < FrameHistorySize)
    {
        ++FrameHistoryCount;
    }

    // 1. OS / window messages. There is no platform backend yet; once one is installed it receives
    //    the frame delta and can request an exit (window closed).
    if (FGenericApplication* Application = FGenericApplication::Get())
    {
        Application->PumpMessages(static_cast<float>(DeltaSeconds));
        if (Application->IsExitRequested())
        {
            RequestExit();
        }
    }

    // 2. The console dashboard. TryEnable() is a no-op once it has succeeded, and stays off when
    //    stdout is redirected, so this needs no switch: the console is simply the current front-end.
    FConsoleDashboard::Get().TryEnable();
    UpdateDashboard();

    // 3. Frame cap. Sleeping here keeps the CPU idle instead of spinning a full core.
    ThrottleFrame(FrameStart);
}

void FEngineLoop::Exit()
{
    // Hand the console back before shutdown logging starts, so those lines are plain text and
    // append below the last dashboard frame instead of being swallowed by it.
    FConsoleDashboard::Get().Disable();

    VENGINE_LOG(LogCore, Info, "[Launch] Engine Exit: Draining async tasks & unloading modules...");

    // 1. Drain worker pool first so no background task runs while module DLLs unload
    FThreadPool::Get().Shutdown();

    // 2. Unload all modules in reverse boot order (safe on partial startup or repeated calls)
    FModuleManager::Get().UnloadAllModules();

    // 3. Audit survivors and destroy class default objects. Must run after every module is gone:
    //    the deleters live in those DLLs.
    FObjectRegistry::Get().Shutdown();

    VENGINE_LOG(LogCore, Info, "[Launch] Engine stopped after {} frame(s).", FrameNumber);

    // Last: flushes the log file and drops the logger. Nothing may log after this point.
    FLog::Shutdown();
}

void FEngineLoop::UpdateDashboard()
{
    const FObjectRegistry& Registry = FObjectRegistry::Get();

    FDashboardStats Stats;
    Stats.Title = "VENGINE";
    Stats.Status = "RUNNING";
    Stats.FrameNumber = FrameNumber;
    Stats.Fps = SmoothedDeltaSeconds > 0.0 ? 1.0 / SmoothedDeltaSeconds : 0.0;
    Stats.FrameMilliseconds = SmoothedDeltaSeconds * 1000.0;
    // Before the first full second has elapsed there is no completed window yet, so report the
    // worst frame seen so far instead of a hard zero.
    Stats.WorstFrameMilliseconds = std::max(WorstReportedSeconds, WorstDeltaSeconds) * 1000.0;
    Stats.UptimeSeconds = FPlatformTime::Seconds() - StartTimeSeconds;
    Stats.LiveObjects = Registry.GetLiveObjectCount();
    Stats.RegisteredClasses = Registry.GetRegisteredClassCount();
    Stats.LoadedModules = FModuleManager::Get().GetLoadedModuleCount();
    Stats.WorkerThreads = static_cast<int32_t>(FThreadPool::Get().GetThreadCount());

    // Rotate the ring so the graph reads oldest -> newest.
    std::array<double, FrameHistorySize> Ordered{};
    for (int32_t Index = 0; Index < FrameHistoryCount; ++Index)
    {
        Ordered[Index] = FrameHistoryMilliseconds[
            (FrameHistoryNext + FrameHistorySize - FrameHistoryCount + Index) % FrameHistorySize];
    }
    Stats.FrameHistory = Ordered.data();
    Stats.FrameHistoryCount = FrameHistoryCount;

    FConsoleDashboard::Get().Render(Stats);
}

void FEngineLoop::ThrottleFrame(double FrameStartSeconds)
{
    if (TargetFrameSeconds <= 0.0)
    {
        return;
    }

    const double Remaining = TargetFrameSeconds - (FPlatformTime::Seconds() - FrameStartSeconds);
    if (Remaining > 0.0)
    {
        FPlatformProcess::SleepSeconds(Remaining);
    }
}