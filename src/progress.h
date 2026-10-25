// Goose compiler — progress: what a person waits on while a command works
// for a while (a fetch, a package's C, the runtime, the link). Each step
// that does real work prints one line saying what it is, wherever the
// output goes. On a terminal a live line below shows what runs at the
// moment, for how long, and the last line the tool running printed, redrawn
// in place and erased when the work is done; anywhere else (a pipe, a file,
// a CI log, an agent reading the output) there is no live line, so nothing
// is printed but those lines. GOOSE_PROGRESS=plain or live overrides what
// stderr suggests. Implemented in progress.cpp, a translation unit of its
// own for the platform headers that ask about the terminal.
#pragma once

namespace goose {

// Work that may take a while, for as long as this object lives. `announce`
// prints `label` as a line of its own as the work starts; otherwise it only
// names the work on the live line. Steps nest: the live line names the
// innermost, unless that one was announced, and times the outermost.
class ProgressStep {
public:
    explicit ProgressStep(const string &label, bool announce = false);
    ~ProgressStep();
    ProgressStep(const ProgressStep &) = delete;
    ProgressStep &operator=(const ProgressStep &) = delete;
};

// Writes `text` to stderr, ending in a line break, with the live line out of
// its way. Whatever is printed while a step lives goes through here.
void ProgressPrint(const string &text);

// What a program started by RunProcess printed, as it comes: its last line
// is what the live line shows.
void ProgressOutput(const char *data, size_t size);

// Whether there is a live line, which a tool can be asked to report
// progress to (git's --progress).
bool ProgressLive();

// Whether errors and warnings on stderr may be colored: `force` (--color
// always), else stderr a terminal and NO_COLOR unset (or CLICOLOR_FORCE
// set). On Windows the console is switched to take the escape sequences;
// false where it cannot be.
bool StderrColor(bool force);

// No live line from here on: for -v and --dry-run, which print the commands
// and what they print as they go.
void ProgressPlain();

}  // namespace goose
