#pragma once
#include "CoreAPI.h"
#include "Logging/Log.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Everything the console header shows. Filled by whoever owns the frame loop; the dashboard only
// formats it, so this stays a view with no dependency on the object registry or the module manager.
struct FDashboardStats
{
    const char* Title = "VENGINE";
    const char* Status = "";

    uint64_t FrameNumber = 0;
    double Fps = 0.0;
    double FrameMilliseconds = 0.0;
    double WorstFrameMilliseconds = 0.0;
    double UptimeSeconds = 0.0;

    int32_t LiveObjects = 0;
    int32_t RegisteredClasses = 0;
    int32_t LoadedModules = 0;
    int32_t WorkerThreads = 0;

    // Optional frame time history, oldest first, in milliseconds. Shown as a graph.
    const double* FrameHistory = nullptr;
    int32_t FrameHistoryCount = 0;
};

// A fixed console panel: ASCII art and live engine stats at the top, a scrolling log tail below.
//
// While enabled the dashboard owns the console: no other code may print, and the regular console
// sink is detached from the logger. Everything is still written to the log file either way.
//
// Threading: Push() may be called from any thread (it is the log sink). TryEnable(), Render() and
// Disable() must be called from the thread that owns the console - the game thread.
class Core_API FConsoleDashboard
{
public:
    static FConsoleDashboard& Get();

    // Returns false when stdout is not an interactive ANSI terminal, or the window is too small.
    // Safe to call repeatedly: once enabled it short-circuits.
    bool TryEnable();
    void Disable();
    bool IsEnabled() const { return bEnabled; }

    // Redraws at most MaxRenderHz, and only the rows that actually changed.
    void Render(const FDashboardStats& Stats);

    // Appends one already-formatted log line (e.g. "[Core] ...message"). Never writes to the
    // console itself, so it is also safe when the dashboard is disabled.
    void Push(ELogVerbosity Verbosity, std::string_view Text);

private:
    FConsoleDashboard();

    struct FLogLine
    {
        ELogVerbosity Verbosity = ELogVerbosity::Info;
        std::string Time;   // HH:MM:SS.mmm
        std::string Text;
    };

    static constexpr uint32_t LineCapacity = 512;
    static constexpr int32_t MaxStoredLineLength = 400;
    static constexpr double MaxRenderHz = 20.0;
    static constexpr int32_t MinWidth = 60;
    static constexpr int32_t MinHeight = 12;

    // Ring buffer: the log tail is kept even before the dashboard is enabled, so enabling it
    // mid-run still shows the startup log.
    mutable std::mutex LineMutex;
    std::vector<FLogLine> Lines;
    uint32_t NextLine = 0;
    uint32_t LineCount = 0;

    bool bEnabled = false;
    bool bHasRendered = false;
    double LastRenderTime = 0.0;
    int32_t LastWidth = 0;
    int32_t LastHeight = 0;
    std::vector<std::string> RowCache;
};