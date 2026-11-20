#pragma once
#include "CoreAPI.h"
#include "Containers/String.h"

struct Core_API FPlatformProcess
{
    // Loads a dynamic library (.dll on Windows, .so on Linux, .dylib on macOS)
    static void* GetDllHandle(const FString& ModuleName);
    static void FreeDllHandle(void* DllHandle);
    static void* GetDllExport(void* DllHandle, const char* ProcName);

    // Looks up an exported symbol from the running host executable (e.g., GetBootModules)
    static void* GetMainExecutableExport(const char* ProcName);

    // Sleeps the calling thread. Negative values return immediately.
    static void SleepSeconds(double Seconds);
};