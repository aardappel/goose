// Goose compiler — progress (progress.h). The live line is drawn by a thread
// of its own, which ticks while any step lives, since the step's own thread
// is blocked reading the tool it runs, which may print nothing for a while.
// It redraws with a carriage return and spaces over what is left of the line
// before, which every terminal takes without escape sequences or console
// modes, and stays a column short of the terminal's width, where a line
// would wrap and no carriage return could take it back.

#include "includes.h"
#include "utils.h"
#include "progress.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace goose {

using Clock = chrono::steady_clock;

struct ProgressState {
    mutex m;
    condition_variable wake;
    thread ticker;
    bool stop = false;
    int mode = -1;                          // Undecided, or whether there is a live line.
    vector<pair<string, bool>> steps;       // Label and whether it was announced, outermost first.
    Clock::time_point start;                // When the outermost step began.
    string detail;                          // The last line the running tool printed,
    string partial;                         // and what it has of the next one.
    int escape = 0;                         // In an escape sequence: after ESC (1) or ESC [ (2).
    size_t shown = 0;                       // The columns the live line takes on screen.
    int spin = 0;
};

// Never destroyed: the ticker must not be a joinable thread at exit.
static ProgressState &State() {
    static auto s = new ProgressState;
    return *s;
}

static bool StderrIsTerminal() {
    #ifdef _WIN32
        // _isatty also holds for NUL. A console is what a live line needs.
        DWORD mode;
        return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) != 0;
    #else
        auto term = getenv("TERM");
        return isatty(STDERR_FILENO) && !(term && !strcmp(term, "dumb"));
    #endif
}

static size_t TerminalColumns() {
    #ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &info))
            return (size_t)(info.srWindow.Right - info.srWindow.Left + 1);
    #else
        winsize ws;
        if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col) return ws.ws_col;
    #endif
    if (auto env = getenv("COLUMNS"); env && atoi(env) > 0) return (size_t)atoi(env);
    return 80;
}

// Under the lock, from here on.

static void Decide(ProgressState &s) {
    if (s.mode >= 0) return;
    auto env = getenv("GOOSE_PROGRESS");
    string want = env ? env : "";
    s.mode = want == "plain" ? 0 : want == "live" ? 1 : StderrIsTerminal();
}

static void Write(const string &text) {
    fwrite(text.data(), 1, text.size(), stderr);
    fflush(stderr);
}

static void Erase(ProgressState &s) {
    if (!s.shown) return;
    Write(cat("\r", string(s.shown, ' '), "\r"));
    s.shown = 0;
}

// Where each of `text`'s columns starts. On Windows a console's code page
// is commonly one byte a character, and tools print in it; elsewhere a
// terminal reads UTF-8.
static vector<size_t> Columns(const string &text) {
    vector<size_t> starts;
    for (size_t i = 0; i < text.size(); i++) {
        #ifndef _WIN32
            if (((uint8_t)text[i] & 0xC0) == 0x80) continue;
        #endif
        starts.push_back(i);
    }
    return starts;
}

// A new step's tool has printed nothing yet.
static void ForgetOutput(ProgressState &s) {
    s.detail.clear();
    s.partial.clear();
    s.escape = 0;
}

static string Elapsed(Clock::duration d) {
    auto secs = (long long)chrono::duration_cast<chrono::seconds>(d).count();
    if (secs < 60) return cat(secs, "s");
    auto two = [](long long n) { return cat(n < 10 ? "0" : "", n); };
    if (secs < 3600) return cat(secs / 60, "m", two(secs % 60), "s");
    return cat(secs / 3600, "h", two(secs / 60 % 60), "m");
}

static void Draw(ProgressState &s) {
    auto cols = TerminalColumns() - 1;
    if (cols < 20) return;
    auto &[label, announced] = s.steps.back();
    auto name = announced ? string() : label;
    auto line = cat("  ", "|/-\\"[s.spin++ % 4], " ", Elapsed(Clock::now() - s.start), "  ", name,
                    !name.empty() && !s.detail.empty() ? ": " : "", s.detail);
    auto starts = Columns(line);
    auto width = starts.size();
    if (width > cols) {
        line = cat(line.substr(0, starts[cols - 3]), "...");
        width = cols;
    }
    Write(cat("\r", line, string(s.shown > width ? s.shown - width : 0, ' ')));
    s.shown = width;
}

static void Tick() {
    auto &s = State();
    unique_lock lock(s.m);
    while (!s.wake.wait_for(lock, chrono::milliseconds(100), [&] { return s.stop; }))
        // A step done within a moment never shows.
        if (!s.steps.empty() && Clock::now() - s.start >= chrono::milliseconds(250)) Draw(s);
}

ProgressStep::ProgressStep(const string &label, bool announce) {
    auto &s = State();
    lock_guard lock(s.m);
    Decide(s);
    fflush(stdout);
    if (announce) {
        Erase(s);
        Write(cat(label, "\n"));
    }
    s.steps.emplace_back(label, announce);
    ForgetOutput(s);
    if (s.mode == 1 && s.steps.size() == 1) {
        s.start = Clock::now();
        s.stop = false;
        s.ticker = thread(Tick);
    }
}

ProgressStep::~ProgressStep() {
    auto &s = State();
    unique_lock lock(s.m);
    s.steps.pop_back();
    ForgetOutput(s);
    if (!s.steps.empty() || !s.ticker.joinable()) return;
    s.stop = true;
    lock.unlock();
    s.wake.notify_all();
    s.ticker.join();
    lock.lock();
    Erase(s);
}

void ProgressPrint(const string &text) {
    auto &s = State();
    lock_guard lock(s.m);
    fflush(stdout);
    Erase(s);
    Write(text.empty() || text.back() != '\n' ? cat(text, "\n") : text);
}

void ProgressOutput(const char *data, size_t size) {
    auto &s = State();
    lock_guard lock(s.m);
    if (s.mode != 1 || s.steps.empty()) return;
    for (size_t i = 0; i < size; i++) {
        auto c = data[i];
        // Colors and cursor movement are dropped: ESC and one character, or
        // ESC [ up to the final character of the sequence.
        if (s.escape == 1) {
            s.escape = c == '[' ? 2 : 0;
            continue;
        }
        if (s.escape == 2) {
            if (c >= 0x40 && c <= 0x7e) s.escape = 0;
            continue;
        }
        if (c == 0x1b) {
            s.escape = 1;
        } else if (c == '\n' || c == '\r') {
            // A carriage return alone ends a line too: git and others redraw
            // their progress that way.
            auto b = s.partial.find_first_not_of(' ');
            if (b != string::npos) s.detail = s.partial.substr(b, s.partial.find_last_not_of(' ') + 1 - b);
            s.partial.clear();
        } else if (s.partial.size() < 1000) {
            if (c == '\t') s.partial += ' ';
            else if ((uint8_t)c >= 0x20 && c != 0x7f) s.partial += c;
        }
    }
}

bool StderrColor(bool force) {
    auto env = [](const char *name) {
        auto v = getenv(name);
        return v && *v ? string(v) : string();
    };
    if (!force) {
        if (!env("NO_COLOR").empty()) return false;
        auto forced = env("CLICOLOR_FORCE");
        force = !forced.empty() && forced != "0";
    }
    if (!force && !StderrIsTerminal()) return false;
    #ifdef _WIN32
        // A console takes the sequences once asked; where stderr is no
        // console (a file, a pipe), they are bytes like any.
        auto h = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode;
        if (GetConsoleMode(h, &mode) &&
            !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            return false;
    #endif
    return true;
}

bool ProgressLive() {
    auto &s = State();
    lock_guard lock(s.m);
    Decide(s);
    return s.mode == 1;
}

void ProgressPlain() {
    auto &s = State();
    lock_guard lock(s.m);
    s.mode = 0;
}

}  // namespace goose
