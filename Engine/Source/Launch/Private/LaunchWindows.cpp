#include "Launch.h"
#include "LaunchEngineLoop.h"

#if defined(_WIN32)
    #include <Windows.h>

    namespace
    {
        // Ctrl+C (and closing the console) requests a clean shutdown instead of terminating the
        // process between two frames. The loop notices at the next frame boundary and unwinds
        // normally: modules shut down, objects are audited, the log file is flushed.
        BOOL WINAPI ConsoleInterruptHandler(DWORD EventType)
        {
            switch (EventType)
            {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                GEngineLoop.RequestExit();
                return TRUE;

            default:
                return FALSE;
            }
        }
    }
#endif

extern "C" Launch_API int EngineMain(int argc, char* argv[])
    {
#if defined(_WIN32)
        SetConsoleCtrlHandler(ConsoleInterruptHandler, TRUE);
#endif

        int ErrorLevel = GEngineLoop.PreInit(argc, argv);
        if (ErrorLevel != 0)
        {
            GEngineLoop.Exit();
            return ErrorLevel;
        }

        ErrorLevel = GEngineLoop.Init();
        if (ErrorLevel == 0)
        {
            while (!GEngineLoop.IsExitRequested())
            {
                GEngineLoop.Tick();
            }
        }

        GEngineLoop.Exit();
        return ErrorLevel;
    }