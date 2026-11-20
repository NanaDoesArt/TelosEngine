#include "Logging/ConsoleDashboard.h"
#include "HAL/Console.h"
#include "HAL/PlatformTime.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <format>
#include <string>
#include <vector>

namespace
{
    // ==========================================================================
    // Presentation constants
    // ==========================================================================
    constexpr const char* AnsiReset = "\x1b[0m";
    constexpr const char* AnsiDim = "\x1b[90m";
    constexpr const char* AnsiBorder = "\x1b[90m";
    constexpr const char* AnsiLabel = "\x1b[36m";
    constexpr const char* AnsiValue = "\x1b[97m";
    constexpr const char* AnsiAccent = "\x1b[96m";
    constexpr const char* AnsiWarn = "\x1b[93m";
    constexpr const char* AnsiError = "\x1b[91m";

    constexpr int32_t LogoHeight = 5;
    constexpr int32_t LogoWidth = 42;
    constexpr const char* Logo[LogoHeight] = {
        "V   V EEEEE N   N  GGGGG  IIIII N   N EEEEE",
        "V   V E     NN  N G         I   NN  N E    ",
        "V   V EEEE  N N N G   GG    I   N N N EEEE ",
        " V V  E     N  NN G    G    I   N  NN E    ",
        "  V   EEEEE N   N  GGGGG  IIIII N   N EEEEE",
    };

    // ==========================================================================
    // Text helpers
    // ==========================================================================
    std::string_view TruncateUtf8(std::string_view Text, int32_t MaxLength)
    {
        if (MaxLength <= 0)
        {
            return {};
        }
        if (static_cast<int32_t>(Text.size()) <= MaxLength)
        {
            return Text;
        }

        size_t Cut = static_cast<size_t>(MaxLength);
        // Never split a multi-byte sequence: step back over continuation bytes (10xxxxxx).
        while (Cut > 0 && (static_cast<unsigned char>(Text[Cut]) & 0xC0) == 0x80)
        {
            --Cut;
        }
        return Text.substr(0, Cut);
    }

    std::string FormatUptime(double Seconds)
    {
        const int64_t Total = Seconds > 0.0 ? static_cast<int64_t>(Seconds) : 0;
        return std::format("{:02}:{:02}:{:02}", Total / 3600, (Total / 60) % 60, Total % 60);
    }

    char SpinnerChar(double NowSeconds)
    {
        static constexpr char Frames[] = { '|', '/', '-', '\\' };
        return Frames[static_cast<int32_t>(NowSeconds * 8.0) & 3];
    }

    char GraphChar(double Milliseconds)
    {
        // Full scale is one 30 FPS frame: anything slower pins the top of the graph.
        static constexpr char Levels[] = " .:-=+*#%@";
        constexpr int32_t LastLevel = 9;
        constexpr double FullScaleMilliseconds = 1000.0 / 30.0;

        double Normalized = Milliseconds / FullScaleMilliseconds;
        if (Normalized < 0.0)
        {
            Normalized = 0.0;
        }
        if (Normalized > 1.0)
        {
            Normalized = 1.0;
        }
        return Levels[static_cast<int32_t>(Normalized * LastLevel + 0.5)];
    }

    std::string_view TagText(ELogVerbosity Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Trace:   return "trace";
        case ELogVerbosity::Verbose: return "verb ";
        case ELogVerbosity::Warning: return "warn ";
        case ELogVerbosity::Error:   return "error";
        case ELogVerbosity::Fatal:   return "fatal";
        case ELogVerbosity::Info:    break;
        }
        return "info ";
    }

    const char* TagColor(ELogVerbosity Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Trace:
        case ELogVerbosity::Verbose: return AnsiDim;
        case ELogVerbosity::Warning: return AnsiWarn;
        case ELogVerbosity::Error:
        case ELogVerbosity::Fatal:   return AnsiError;
        case ELogVerbosity::Info:    break;
        }
        return AnsiLabel;
    }

    const char* TextColor(ELogVerbosity Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Trace:
        case ELogVerbosity::Verbose: return AnsiDim;
        case ELogVerbosity::Warning: return AnsiWarn;
        case ELogVerbosity::Error:
        case ELogVerbosity::Fatal:   return AnsiError;
        case ELogVerbosity::Info:    break;
        }
        return nullptr;
    }

    std::string CurrentTimeString()
    {
        using namespace std::chrono;

        const auto Now = system_clock::now();
        const std::time_t Seconds = system_clock::to_time_t(Now);
        const int64_t Milliseconds = duration_cast<milliseconds>(Now.time_since_epoch()).count() % 1000;

        std::tm Local{};
#if defined(_WIN32)
        localtime_s(&Local, &Seconds);
#else
        localtime_r(&Seconds, &Local);
#endif

        return std::format(
            "{:02}:{:02}:{:02}.{:03}",
            Local.tm_hour,
            Local.tm_min,
            Local.tm_sec,
            static_cast<int>(Milliseconds)
        );
    }

    // ==========================================================================
    // Row builder
    //
    // Tracks the visible length separately from the buffer, so ANSI color codes never affect
    // padding or truncation. Every colored segment resets afterwards: a row can never leak its
    // color into the rest of the screen.
    // ==========================================================================
    class FRow
    {
    public:
        FRow(std::string& InBuffer, int32_t InWidth)
            : Buffer(InBuffer)
            , Width(InWidth)
        {
        }

        void Colored(const char* Color, std::string_view Text)
        {
            if (Remaining() <= 0)
            {
                return;
            }

            const std::string_view Piece = TruncateUtf8(Text, Remaining());
            if (Piece.empty())
            {
                return;
            }

            if (Color)
            {
                Buffer.append(Color);
            }
            Buffer.append(Piece.data(), Piece.size());
            if (Color)
            {
                Buffer.append(AnsiReset);
            }
            Visible += static_cast<int32_t>(Piece.size());
        }

        void Plain(std::string_view Text) { Colored(nullptr, Text); }

        void PadTo(int32_t TargetVisible)
        {
            if (TargetVisible > Visible)
            {
                Buffer.append(static_cast<size_t>(TargetVisible - Visible), ' ');
                Visible = TargetVisible;
            }
        }

        int32_t Remaining() const { return Width - Visible; }

    private:
        std::string& Buffer;
        int32_t Width;
        int32_t Visible = 0;
    };

    // A stat cell is all-or-nothing: half a number on a narrow terminal reads worse than nothing.
    void AddCell(FRow& Row, std::string_view Label, std::string_view Value)
    {
        constexpr int32_t LabelWidth = 8;
        constexpr int32_t ValueWidth = 10;
        constexpr int32_t CellWidth = LabelWidth + 1 + ValueWidth + 2;

        if (Row.Remaining() < CellWidth)
        {
            return;
        }

        const int32_t LabelPadding = std::max(0, LabelWidth - static_cast<int32_t>(Label.size()));
        const int32_t ValuePadding = std::max(0, ValueWidth - static_cast<int32_t>(Value.size()));

        Row.Colored(AnsiLabel, Label);
        Row.Plain(std::string(static_cast<size_t>(LabelPadding), ' '));
        Row.Plain(" ");
        Row.Colored(AnsiValue, Value);
        Row.Plain(std::string(static_cast<size_t>(ValuePadding + 2), ' '));
    }

    void AddPair(FRow& Row, std::string_view Label, std::string_view Value)
    {
        // Whole pair or nothing: "THR 1" left over from a truncated "THR 11" reads like a
        // different number.
        const int32_t Needed = static_cast<int32_t>(Label.size() + Value.size()) + 4;
        if (Row.Remaining() < Needed)
        {
            return;
        }

        Row.Colored(AnsiLabel, Label);
        Row.Plain(" ");
        Row.Colored(AnsiValue, Value);
        Row.Plain("   ");
    }

    void AddPlainBorder(std::vector<std::string>& Rows, int32_t UsableWidth)
    {
        std::string& Border = Rows.emplace_back();
        FRow Row(Border, UsableWidth);
        Row.Colored(AnsiBorder, "+");
        Row.Colored(AnsiBorder, std::string(static_cast<size_t>(UsableWidth) - 2, '-'));
        Row.Colored(AnsiBorder, "+");
    }

    void AddTitleBorder(std::vector<std::string>& Rows, int32_t UsableWidth, const FDashboardStats& Stats)
    {
        std::string& Border = Rows.emplace_back();
        FRow Row(Border, UsableWidth);

        Row.Colored(AnsiBorder, "+- ");
        Row.Colored(AnsiAccent, Stats.Title);

        if (Stats.Status && *Stats.Status)
        {
            Row.Plain(" ");
            Row.Colored(AnsiLabel, "[");
            Row.Colored(AnsiValue, Stats.Status);
            Row.Colored(AnsiLabel, "]");
        }

        Row.Plain(" ");

        // Leave room for the closing '+'.
        const int32_t Dashes = Row.Remaining() - 1;
        if (Dashes > 0)
        {
            Row.Colored(AnsiBorder, std::string(static_cast<size_t>(Dashes), '-'));
        }
        Row.Colored(AnsiBorder, "+");
    }
}

FConsoleDashboard& FConsoleDashboard::Get()
{
    static FConsoleDashboard Instance;
    return Instance;
}

FConsoleDashboard::FConsoleDashboard()
{
    Lines.resize(LineCapacity);
}

bool FConsoleDashboard::TryEnable()
{
    if (bEnabled)
    {
        return true;
    }

    if (!FConsole::SupportsAnsi())
    {
        return false;
    }

    if (FConsole::GetWidth() < MinWidth || FConsole::GetHeight() < MinHeight)
    {
        return false;
    }

    bEnabled = true;
    bHasRendered = false;
    LastRenderTime = 0.0;
    LastWidth = 0;
    LastHeight = 0;
    RowCache.clear();

    // The dashboard paints the console itself now, so the plain console sink must stop.
    FLog::SetConsoleDashboardActive(true);
    FConsole::SetCursorVisible(false);
    return true;
}

void FConsoleDashboard::Disable()
{
    if (!bEnabled)
    {
        return;
    }

    bEnabled = false;
    bHasRendered = false;
    RowCache.clear();

    // Restore plain console logging first, then park the cursor on a fresh line below the panel so
    // shutdown output appends instead of overwriting the last panel row.
    FLog::SetConsoleDashboardActive(false);
    FConsole::SetCursorVisible(true);

    if (LastHeight > 0)
    {
        FConsole::Write(std::format("\x1b[{};1H\n", LastHeight));
    }
}

void FConsoleDashboard::Push(ELogVerbosity Verbosity, std::string_view Text)
{
    // A log message may contain newlines (exception text, multi-line warnings). The panel is a
    // fixed grid, so they become spaces, and control characters that would move the cursor are
    // dropped entirely.
    std::string Clean;
    Clean.reserve(std::min<size_t>(Text.size(), static_cast<size_t>(MaxStoredLineLength)));
    for (const char Character : Text)
    {
        if (static_cast<int32_t>(Clean.size()) >= MaxStoredLineLength)
        {
            break;
        }

        if (Character == '\n' || Character == '\r')
        {
            if (!Clean.empty() && Clean.back() == ' ')
            {
                continue;
            }
            Clean.push_back(' ');
        }
        else if (Character == '\t')
        {
            Clean.push_back(' ');
        }
        else if (static_cast<unsigned char>(Character) >= 0x20)
        {
            Clean.push_back(Character);
        }
    }

    std::string Time = CurrentTimeString();

    std::lock_guard<std::mutex> Lock(LineMutex);
    FLogLine& Slot = Lines[NextLine];
    Slot.Verbosity = Verbosity;
    Slot.Time = std::move(Time);
    Slot.Text = std::move(Clean);

    NextLine = (NextLine + 1) % LineCapacity;
    if (LineCount < LineCapacity)
    {
        ++LineCount;
    }
}

void FConsoleDashboard::Render(const FDashboardStats& Stats)
{
    if (!bEnabled)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    if (bHasRendered && (Now - LastRenderTime) < (1.0 / MaxRenderHz))
    {
        return;
    }
    LastRenderTime = Now;
    bHasRendered = true;

    const int32_t Width = FConsole::GetWidth();
    const int32_t Height = FConsole::GetHeight();

    // Shrinking a terminal mid-run is common. Hand the console back instead of drawing garbage.
    if (Width < MinWidth || Height < MinHeight)
    {
        Disable();
        return;
    }

    const bool bResized = Width != LastWidth || Height != LastHeight;
    LastWidth = Width;
    LastHeight = Height;

    // The last column is never written: a character there makes some terminals auto-wrap and
    // scroll, which would shift every absolute cursor position below.
    const int32_t UsableWidth = Width - 1;
    const int32_t InnerWidth = UsableWidth - 4;   // "| " + content + " |"

    const bool bArt = InnerWidth >= LogoWidth + 30 && Height >= LogoHeight + 10;

    std::vector<std::string> NewRows;
    NewRows.reserve(static_cast<size_t>(Height));

    AddTitleBorder(NewRows, UsableWidth, Stats);

    if (bArt)
    {
        const int32_t AnimationPhase = static_cast<int32_t>(Now * 2.0) & 1;
        const char* LogoColor = AnimationPhase == 0 ? AnsiAccent : AnsiLabel;

        for (int32_t Index = 0; Index < LogoHeight; ++Index)
        {
            std::string& RowText = NewRows.emplace_back();
            FRow Row(RowText, UsableWidth);

            Row.Colored(AnsiBorder, "| ");
            Row.Colored(LogoColor, Logo[Index]);

            const int32_t LogoLength = static_cast<int32_t>(std::strlen(Logo[Index]));
            if (LogoLength < LogoWidth)
            {
                Row.Plain(std::string(static_cast<size_t>(LogoWidth - LogoLength), ' '));
            }

            Row.Colored(AnsiBorder, " | ");

            switch (Index)
            {
            case 0:
                AddCell(Row, "FPS", std::format("{:.1f}", Stats.Fps));
                AddCell(Row, "FRAME", std::format("{}", Stats.FrameNumber));
                // Right-aligned spinner: the panel proves it is alive even when nothing changes.
                Row.PadTo(UsableWidth - 2);
                Row.Colored(AnsiAccent, std::string(1, SpinnerChar(Now)));
                break;

            case 1:
                AddCell(Row, "TIME", std::format("{:.2f} ms", Stats.FrameMilliseconds));
                AddCell(Row, "PEAK", std::format("{:.2f} ms", Stats.WorstFrameMilliseconds));
                break;

            case 2:
                AddCell(Row, "OBJECTS", std::format("{}", Stats.LiveObjects));
                AddCell(Row, "CLASSES", std::format("{}", Stats.RegisteredClasses));
                break;

            case 3:
                AddCell(Row, "MODULES", std::format("{}", Stats.LoadedModules));
                AddCell(Row, "THREADS", std::format("{}", Stats.WorkerThreads));
                break;

            default:
            {
                AddCell(Row, "UPTIME", FormatUptime(Stats.UptimeSeconds));

                const int32_t Available = Row.Remaining() - 2;
                if (Available > 0 && Stats.FrameHistory && Stats.FrameHistoryCount > 0)
                {
                    const int32_t First = std::max(0, Stats.FrameHistoryCount - Available);
                    std::string Graph;
                    Graph.reserve(static_cast<size_t>(Stats.FrameHistoryCount - First));
                    for (int32_t Sample = First; Sample < Stats.FrameHistoryCount; ++Sample)
                    {
                        Graph.push_back(GraphChar(Stats.FrameHistory[Sample]));
                    }
                    Row.Colored(AnsiAccent, Graph);
                }
                break;
            }
            }

            Row.PadTo(UsableWidth - 1);
            Row.Colored(AnsiBorder, "|");
        }

        AddPlainBorder(NewRows, UsableWidth);
    }
    else
    {
        // Not enough room for the art: one line of the same numbers under the title.
        std::string& RowText = NewRows.emplace_back();
        FRow Row(RowText, UsableWidth);

        Row.Colored(AnsiBorder, "| ");
        AddPair(Row, "FPS", std::format("{:.1f}", Stats.Fps));
        AddPair(Row, "TIME", std::format("{:.2f} ms", Stats.FrameMilliseconds));
        AddPair(Row, "FRAME", std::format("{}", Stats.FrameNumber));
        AddPair(Row, "OBJ", std::format("{}", Stats.LiveObjects));
        AddPair(Row, "CLS", std::format("{}", Stats.RegisteredClasses));
        AddPair(Row, "MOD", std::format("{}", Stats.LoadedModules));
        AddPair(Row, "THR", std::format("{}", Stats.WorkerThreads));
        Row.PadTo(UsableWidth - 2);
        Row.Colored(AnsiAccent, std::string(1, SpinnerChar(Now)));
        Row.PadTo(UsableWidth - 1);
        Row.Colored(AnsiBorder, "|");

        AddPlainBorder(NewRows, UsableWidth);
    }

    const int32_t HeaderRows = static_cast<int32_t>(NewRows.size());
    const int32_t LogRows = Height - HeaderRows;

    // Copy the tail under the lock, then format and print without holding it: a slow terminal must
    // never stall a worker thread that is trying to log.
    std::vector<FLogLine> Snapshot;
    {
        std::lock_guard<std::mutex> Lock(LineMutex);
        Snapshot.reserve(LineCount);
        for (uint32_t Index = 0; Index < LineCount; ++Index)
        {
            Snapshot.push_back(Lines[(NextLine + LineCapacity - LineCount + Index) % LineCapacity]);
        }
    }

    const int32_t VisibleLines = std::min(LogRows, static_cast<int32_t>(Snapshot.size()));
    const int32_t FirstLine = static_cast<int32_t>(Snapshot.size()) - VisibleLines;

    for (int32_t Index = 0; Index < LogRows; ++Index)
    {
        std::string& RowText = NewRows.emplace_back();
        FRow Row(RowText, UsableWidth);

        Row.Colored(AnsiBorder, "| ");

        if (Index < VisibleLines)
        {
            const FLogLine& Line = Snapshot[static_cast<size_t>(FirstLine + Index)];
            Row.Colored(AnsiDim, Line.Time);
            Row.Plain(" ");
            Row.Colored(TagColor(Line.Verbosity), TagText(Line.Verbosity));
            Row.Plain(" ");
            Row.Colored(TextColor(Line.Verbosity), Line.Text);
        }

        Row.PadTo(UsableWidth - 1);
        Row.Colored(AnsiBorder, "|");
    }

    // Only rows whose text changed are written. A steady 20 Hz panel costs a few hundred bytes per
    // frame instead of a full repaint, and the screen does not flicker.
    std::string Output;
    Output.reserve(4096);

    if (bResized || RowCache.size() != NewRows.size())
    {
        Output += "\x1b[2J";
        RowCache.assign(NewRows.size(), std::string());
    }

    for (size_t Index = 0; Index < NewRows.size(); ++Index)
    {
        if (NewRows[Index] == RowCache[Index])
        {
            continue;
        }

        Output += std::format("\x1b[{};1H", Index + 1);
        Output += NewRows[Index];
        Output += "\x1b[K";
        RowCache[Index] = NewRows[Index];
    }

    if (!Output.empty())
    {
        FConsole::Write(Output);
    }
}