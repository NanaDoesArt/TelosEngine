#include "HAL/PlatformProcess.h"

#include <chrono>
#include <thread>

#if defined(_WIN32)
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

void* FPlatformProcess::GetDllHandle(const FString& ModuleName)
{
    // Convert "Engine_Core" or "Engine::Core" -> "Engine-Core"
    FString FormattedName = ModuleName;
    // (Or pass "Engine_Core" from manifest and format "VEngine-Engine-Core")
    std::string Raw(FormattedName.CStr());
    for (size_t pos = 0; (pos = Raw.find("::", pos)) != std::string::npos; )
    {
        Raw.replace(pos, 2, "-");
    }
    for (char& c : Raw)
    {
        if (c == '_') c = '-';
    }

#if defined(_WIN32)
    FString FileName = FString("VEngine-") + Raw.c_str() + ".dll";
    return static_cast<void*>(LoadLibraryA(FileName.CStr()));
#elif defined(__APPLE__)
    FString FileName = FString("libVEngine-") + Raw.c_str() + ".dylib";
    return dlopen(FileName.CStr(), RTLD_LAZY | RTLD_LOCAL);
#else
    FString FileName = FString("libVEngine-") + Raw.c_str() + ".so";
    return dlopen(FileName.CStr(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void FPlatformProcess::FreeDllHandle(void* DllHandle)
{
    if (!DllHandle)
    {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(DllHandle));
#else
    dlclose(DllHandle);
#endif
}

void* FPlatformProcess::GetDllExport(void* DllHandle, const char* ProcName)
{
    if (!DllHandle || !ProcName)
    {
        return nullptr;
    }

#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(DllHandle), ProcName));
#else
    return dlsym(DllHandle, ProcName);
#endif
}

void* FPlatformProcess::GetMainExecutableExport(const char* ProcName)
{
    if (!ProcName)
    {
        return nullptr;
    }

#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA(nullptr), ProcName));
#else
    void* MainHandle = dlopen(nullptr, RTLD_LAZY);
    if (!MainHandle)
    {
        return nullptr;
    }
    void* Symbol = dlsym(MainHandle, ProcName);
    dlclose(MainHandle);
    return Symbol;
#endif
}

void FPlatformProcess::SleepSeconds(double Seconds)
{
    if (Seconds <= 0.0)
    {
        return;
    }

#if defined(_WIN32)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

    // A high-resolution waitable timer is precise to well under a millisecond without touching
    // the system-wide timer resolution the way timeBeginPeriod() does. Plain Sleep() rounds up to
    // the 15.6 ms tick, which turns a 60 FPS frame budget into 30 FPS.
    static HANDLE Timer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );

    if (Timer)
    {
        LARGE_INTEGER DueTime;
        DueTime.QuadPart = -static_cast<LONGLONG>(Seconds * 10'000'000.0);   // 100 ns units
        if (DueTime.QuadPart != 0
            && SetWaitableTimer(Timer, &DueTime, 0, nullptr, nullptr, FALSE))
        {
            WaitForSingleObject(Timer, INFINITE);
            return;
        }
    }
#endif

    // Fallback (and the whole POSIX path): sleep, then yield out the last millisecond in case the
    // OS rounded the sleep up to its timer tick.
    const auto Target = std::chrono::steady_clock::now() + std::chrono::duration<double>(Seconds);

    constexpr double SpinThresholdSeconds = 0.001;
    if (Seconds > SpinThresholdSeconds)
    {
        std::this_thread::sleep_for(std::chrono::duration<double>(Seconds - SpinThresholdSeconds));
    }
    while (std::chrono::steady_clock::now() < Target)
    {
        std::this_thread::yield();
    }
}