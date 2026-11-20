#include "HAL/Console.h"

#include <cstdio>

#if defined(_WIN32)
    #include <Windows.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        // Not defined by older SDKs; the value has been stable since Windows 10.
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace
{
    struct FConsoleState
    {
        bool bInitialized = false;
        bool bAnsi = false;
    };

    FConsoleState& State()
    {
        static FConsoleState Instance;
        return Instance;
    }

#if defined(_WIN32)
    HANDLE OutputHandle()
    {
        return GetStdHandle(STD_OUTPUT_HANDLE);
    }

    bool IsConsoleHandle(HANDLE Handle)
    {
        DWORD Mode = 0;
        return Handle != nullptr
            && Handle != INVALID_HANDLE_VALUE
            && GetConsoleMode(Handle, &Mode) != 0;
    }
#endif
}

bool FConsole::SupportsAnsi()
{
    FConsoleState& ConsoleState = State();
    if (ConsoleState.bInitialized)
    {
        return ConsoleState.bAnsi;
    }
    ConsoleState.bInitialized = true;

#if defined(_WIN32)
    HANDLE Handle = OutputHandle();
    if (!IsConsoleHandle(Handle))
    {
        // Piped to a file or another process: never emit escape sequences there.
        return false;
    }

    DWORD Mode = 0;
    GetConsoleMode(Handle, &Mode);
    if ((Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0)
    {
        if (!SetConsoleMode(Handle, Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT))
        {
            // A pre-Windows 10 console: no ANSI support, so no full-screen dashboard.
            return false;
        }
    }

    ConsoleState.bAnsi = true;
#else
    ConsoleState.bAnsi = isatty(fileno(stdout)) != 0;
#endif

    return ConsoleState.bAnsi;
}

int32_t FConsole::GetWidth()
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO Info{};
    if (IsConsoleHandle(OutputHandle()) && GetConsoleScreenBufferInfo(OutputHandle(), &Info))
    {
        return static_cast<int32_t>(Info.srWindow.Right - Info.srWindow.Left + 1);
    }
    return 100;
#else
    winsize Size{};
    if (ioctl(fileno(stdout), TIOCGWINSZ, &Size) == 0 && Size.ws_col > 0)
    {
        return static_cast<int32_t>(Size.ws_col);
    }
    return 100;
#endif
}

int32_t FConsole::GetHeight()
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO Info{};
    if (IsConsoleHandle(OutputHandle()) && GetConsoleScreenBufferInfo(OutputHandle(), &Info))
    {
        return static_cast<int32_t>(Info.srWindow.Bottom - Info.srWindow.Top + 1);
    }
    return 30;
#else
    winsize Size{};
    if (ioctl(fileno(stdout), TIOCGWINSZ, &Size) == 0 && Size.ws_row > 0)
    {
        return static_cast<int32_t>(Size.ws_row);
    }
    return 30;
#endif
}

void FConsole::Write(std::string_view Text)
{
    if (Text.empty())
    {
        return;
    }

    std::fwrite(Text.data(), 1, Text.size(), stdout);
    std::fflush(stdout);
}

void FConsole::SetCursorVisible(bool bVisible)
{
    if (!SupportsAnsi())
    {
        return;
    }

    Write(bVisible ? "\x1b[?25h" : "\x1b[?25l");
}