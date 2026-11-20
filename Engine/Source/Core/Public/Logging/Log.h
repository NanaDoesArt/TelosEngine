#pragma once
#include "Core.h"
#include <format>
#include <string_view>

enum class ELogVerbosity
{
    Trace,
    Info,
    Verbose,
    Warning,
    Error,
    Fatal
};

struct FLogCategory
{
    const char* Name;
    ELogVerbosity DefaultVerbosity;
};

class Core_API FLog
{
    public:
    static void Init();
    static void Shutdown();
    static void LogMessage(const char* CategoryName, ELogVerbosity Verbosity, std::string_view Message);

    // Routes console output through the dashboard instead of the plain color sink, or back.
    // Called by FConsoleDashboard. Safe before Init(): the choice is applied when the logger is
    // created, so the very first log line already follows it.
    static void SetConsoleDashboardActive(bool bActive);
};

// Declares a log category across DLL boundaries (e.g., DECLARE_LOG_CATEGORY_EXTERN(Core_API, LogCore, Info))
#define DECLARE_LOG_CATEGORY_EXTERN(API_MACRO, CategoryName, DefaultVerbosity) \
    extern API_MACRO FLogCategory CategoryName;

// Defines the log category in a .cpp file (e.g., DEFINE_LOG_CATEGORY(LogCore, Info))
#define DEFINE_LOG_CATEGORY(CategoryName, DefaultVerbosity) \
    FLogCategory CategoryName{ #CategoryName, ELogVerbosity::DefaultVerbosity };

// Core engine log categories exported from Core.dll
DECLARE_LOG_CATEGORY_EXTERN(Core_API, LogCore, Info)
DECLARE_LOG_CATEGORY_EXTERN(Core_API, LogTemp, Info)

// Main logging macro: VENGINE_LOG(LogCore, Info, "Loaded {} modules", Count)
#define VENGINE_LOG(Category, Verbosity, FormatStr, ...) \
    do \
    { \
        if (ELogVerbosity::Verbosity >= (Category).DefaultVerbosity) \
        { \
            FLog::LogMessage((Category).Name, ELogVerbosity::Verbosity, std::format(FormatStr, ##__VA_ARGS__)); \
        } \
    } while (0)