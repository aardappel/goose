#include "includes.h"
#include "process.h"

// Call the production formatter. Do not copy its quoting logic into the test.
int main(int argc, char **argv) {
    goose::vector<goose::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    auto text = goose::CommandLineText(args);
    fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}
