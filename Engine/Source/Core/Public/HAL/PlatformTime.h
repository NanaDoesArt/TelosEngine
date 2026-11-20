#pragma once
#include "Core.h"
#include <cstdint>

struct Core_API FPlatformTime
{
    static void InitTiming();
    static double Seconds();
    static uint64_t Cycles64();
    static double ToMilliseconds(uint64_t Cycles);
};