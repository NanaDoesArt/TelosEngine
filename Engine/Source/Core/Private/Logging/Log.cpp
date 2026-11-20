#include "Logging/Log.h"
#include "Logging/ConsoleDashboard.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>

DEFINE_LOG_CATEGORY(LogCore, Info)
DEFINE_LOG_CATEGORY(LogTemp, Info)

namespace
{
    std::mutex LogMutex;
    std::shared_ptr<spdlog::logger> EngineLogger;

    // Console output goes through a router so the dashboard can take the console over and give it
    // back at runtime. The file sink is never routed: the log file always has every line.
    std::shared_ptr<spdlog::sinks::dist_sink_mt> ConsoleRouter;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> ConsoleSink;
    std::shared_ptr<spdlog::sinks::sink> DashboardSink;

    bool bDashboardOwnsConsole = false;

    spdlog::level::level_enum ToSpdlogLevel(ELogVerbosity Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Trace:   return spdlog::level::trace;
        case ELogVerbosity::Verbose: return spdlog::level::debug;
        case ELogVerbosity::Warning: return spdlog::level::warn;
        case ELogVerbosity::Error:   return spdlog::level::err;
        case ELogVerbosity::Fatal:   return spdlog::level::critical;
        case ELogVerbosity::Info:    break;
        }
        return spdlog::level::info;
    }

    ELogVerbosity FromSpdlogLevel(spdlog::level::level_enum Level)
    {
        switch (Level)
        {
        case spdlog::level::trace:    return ELogVerbosity::Trace;
        case spdlog::level::debug:    return ELogVerbosity::Verbose;
        case spdlog::level::warn:     return ELogVerbosity::Warning;
        case spdlog::level::err:      return ELogVerbosity::Error;
        case spdlog::level::critical: return ELogVerbosity::Fatal;
        case spdlog::level::info:     break;
        default:                      break;
        }
        return ELogVerbosity::Info;
    }

    // Feeds every message into the dashboard's fixed-size log tail. It never writes to the console
    // itself: while the dashboard is disabled the regular color sink sits next to it in the router.
    class FDashboardSink final : public spdlog::sinks::base_sink<std::mutex>
    {
    protected:
        void sink_it_(const spdlog::details::log_msg& Message) override
        {
            FConsoleDashboard::Get().Push(
                FromSpdlogLevel(Message.level),
                std::string_view(Message.payload.data(), Message.payload.size())
            );
        }

        void flush_() override {}
    };

    // Assumes LogMutex is held.
    void ApplyConsoleRouting()
    {
        if (!ConsoleRouter)
        {
            return;
        }

        if (bDashboardOwnsConsole)
        {
            // The dashboard paints the console itself; nothing may also print to it.
            ConsoleRouter->set_sinks({ DashboardSink });
        }
        else
        {
            ConsoleRouter->set_sinks({ ConsoleSink, DashboardSink });
        }
    }

    // Assumes LogMutex is held.
    void InitLocked()
    {
        if (EngineLogger)
        {
            return;
        }

        std::filesystem::create_directories("Saved/Logs");

        ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        ConsoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

        DashboardSink = std::make_shared<FDashboardSink>();

        ConsoleRouter = std::make_shared<spdlog::sinks::dist_sink_mt>();
        ApplyConsoleRouting();

        auto FileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Saved/Logs/VEngine.log", true);
        FileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        EngineLogger = std::make_shared<spdlog::logger>(
            "VEngine",
            spdlog::sinks_init_list{ ConsoleRouter, FileSink }
        );
        EngineLogger->set_level(spdlog::level::trace);

        // Flush every message so a crash never loses the lines leading up to it.
        EngineLogger->flush_on(spdlog::level::trace);
    }
}

void FLog::Init()
{
    std::lock_guard<std::mutex> Lock(LogMutex);
    InitLocked();
}

void FLog::Shutdown()
{
    std::lock_guard<std::mutex> Lock(LogMutex);

    if (EngineLogger)
    {
        EngineLogger->flush();
        EngineLogger.reset();
    }

    ConsoleRouter.reset();
    ConsoleSink.reset();
    DashboardSink.reset();

    spdlog::shutdown();
}

void FLog::SetConsoleDashboardActive(bool bActive)
{
    std::lock_guard<std::mutex> Lock(LogMutex);
    bDashboardOwnsConsole = bActive;
    ApplyConsoleRouting();
}

void FLog::LogMessage(const char* CategoryName, ELogVerbosity Verbosity, std::string_view Message)
{
    // Hold a reference for the duration of the call: Shutdown() could otherwise reset the logger
    // while this thread is inside spdlog.
    std::shared_ptr<spdlog::logger> Logger;
    {
        std::lock_guard<std::mutex> Lock(LogMutex);
        InitLocked();
        Logger = EngineLogger;
    }

    if (Verbosity == ELogVerbosity::Fatal)
    {
        // A fatal message has to be readable even while the dashboard owns the console. Hand it
        // back first, then write the line as plain output.
        FConsoleDashboard::Get().Disable();
        Logger->critical("[{}] {}", CategoryName, Message);
        Logger->flush();
        std::abort();
    }

    Logger->log(ToSpdlogLevel(Verbosity), "[{}] {}", CategoryName, Message);
}