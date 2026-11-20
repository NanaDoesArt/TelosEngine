#pragma once
#include "CoreAPI.h"
#include <cstdint>
#include <string_view>

// The console is the engine's default front-end until a window backend exists. Everything that
// needs to know whether stdout is a real terminal, and how big it is, asks here instead of
// carrying its own platform #ifdefs.
struct Core_API FConsole
{
    // True when stdout is an interactive terminal and ANSI escape sequences are understood.
    // The first call also enables ANSI processing where the platform requires opting in
    // (Windows 10+). Cached: the answer cannot change during a run.
    static bool SupportsAnsi();

    // Visible window size in character cells.
    static int32_t GetWidth();
    static int32_t GetHeight();

    // Writes bytes to stdout and flushes. Nothing else in the engine may write to stdout while
    // a full-screen consumer (the dashboard) owns it.
    static void Write(std::string_view Text);

    static void SetCursorVisible(bool bVisible);
};