#include "HAL/PlatformTime.h"
#include <chrono>

namespace
{
    using FClock = std::chrono::steady_clock;
    FClock::time_point StartTime = FClock::now();
}

void FPlatformTime::InitTiming()
{
    StartTime = FClock::now();
}

double FPlatformTime::Seconds()
{
    const auto Now = FClock::now();
    return std::chrono::duration<double>(Now - StartTime).count();
}

uint64_t FPlatformTime::Cycles64()
{
    const auto Now = FClock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Now - StartTime).count());
}

double FPlatformTime::ToMilliseconds(uint64_t Cycles)
{
    return static_cast<double>(Cycles) / 1'000'000.0;
}