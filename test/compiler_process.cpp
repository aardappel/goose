// RunProcess sends no input. A child must see EOF before the parent joins it,
// even when the child first writes more output than a pipe can hold.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/includes.h"
#include "../src/process.h"
#include <filesystem>

using namespace goose;

int main(int argc, char **argv) {
    if (argc == 2) {
        // End the child on a deadlock, so the parent can report a failure and
        // reap it. This is a test timeout, not a performance measurement.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            // The output pipe may be full. Exit without writing to it.
            _Exit(124);
        }).detach();
        if (!strcmp(argv[1], "--output-first")) {
            string block(256 * 1024, 'x');
            assert(fwrite(block.data(), 1, block.size(), stdout) == block.size());
            assert(fflush(stdout) == 0);
            assert(fwrite(block.data(), 1, block.size(), stderr) == block.size());
            assert(fflush(stderr) == 0);
        } else {
            assert(!strcmp(argv[1], "--stdin-first"));
        }
        assert(fgetc(stdin) == EOF && !ferror(stdin));
        fputs("eof", stdout);
        return 23;
    }

    auto exe = std::filesystem::absolute(argv[0]).string();
    for (bool output_first : { false, true }) {
        auto result = RunProcess({ exe, output_first ? "--output-first" : "--stdin-first" },
                                 "", !output_first);
        if (!result.started || result.code != 23) {
            fprintf(stderr, "child did not complete after stdin EOF (started=%d, code=%d): %s\n",
                    result.started, result.code, result.output.c_str());
            return 1;
        }
        auto expected = (output_first ? string(512 * 1024, 'x') : string()) + "eof";
        assert(result.output == expected);
    }
    puts("process stdin EOF and output capture: passed");
}
