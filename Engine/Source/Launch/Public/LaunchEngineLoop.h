#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// The engine loop. There is exactly one, and its behaviour is not configurable from the command
// line: a shipped game receives whatever arguments the player passes, so a launch switch that
// changed the loop would be a player-facing control surface. Configuration lives in code (see
// TargetFrameRate) until there is a real settings system behind it.
class FEngineLoop
{
public:
    // argc/argv are accepted because the platform entry point hands them over, and a project will
    // eventually need them (map to open, replay file). Nothing reads them today.
    int PreInit(int argc, char* argv[]);
    int Init();
    void Tick();
    void Exit();

    bool IsExitRequested() const { return bExitRequested.load(std::memory_order_relaxed); }

    // Safe from any thread. The Windows console handler runs on its own thread, and the loop
    // notices at the next frame boundary.
    void RequestExit() { bExitRequested.store(true, std::memory_order_relaxed); }

    // Frames per second the loop paces itself to; 0 means uncapped. This is engine policy, not a
    // launch option: it stays a constant until there is a settings system a project can write to.
    static constexpr double TargetFrameRate = 60.0;

private:
    void UpdateDashboard();
    void ThrottleFrame(double FrameStartSeconds);

    std::atomic<bool> bExitRequested{ false };

    uint64_t FrameNumber = 0;
    double StartTimeSeconds = 0.0;
    double LastTickTimeSeconds = 0.0;
    double SmoothedDeltaSeconds = 0.0;
    double WorstDeltaSeconds = 0.0;
    double WorstWindowStartSeconds = 0.0;
    double WorstReportedSeconds = 0.0;

    static constexpr int32_t FrameHistorySize = 64;
    std::array<double, FrameHistorySize> FrameHistoryMilliseconds{};
    int32_t FrameHistoryCount = 0;
    int32_t FrameHistoryNext = 0;

    // A debugger break or a stalled disk must not be reported as one enormous frame.
    static constexpr double MaxDeltaSeconds = 0.25;
    // ~0.16 s time constant at 60 FPS: smooth enough to read, fast enough to react.
    static constexpr double DeltaSmoothing = 0.1;
    static constexpr double TargetFrameSeconds = TargetFrameRate > 0.0 ? (1.0 / TargetFrameRate) : 0.0;
};

// Locked inside Launch (no Launch_API) so Game/Editor modules cannot call it directly
extern FEngineLoop GEngineLoop;