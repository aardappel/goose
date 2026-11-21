// The extern symbol names a native part's table carries: a name the lexer
// takes in from a string literal, without the typecheck's C-identifier
// validation (ExternNamesIn asks files the program never parsed), must not
// reach the generated goose_symbols.c as raw text: the identifier
// positions take a validated C identifier, the literal one C escaping.
// Keep assertions active even in an optimized test build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/includes.h"
#include "../src/utils.h"
#include "../src/extern_names.h"

using namespace goose;

int main() {
    // A quote smuggled through a string literal: the goose source carries
    // it as an \x22 escape, which the lexer decodes into the name's text.
    set<string> names;
    try {
        ExternNamesIn("x.goose", "extern fn f() -> i64 = \"a\\x22quote\";", names, nullptr);
    } catch (CompileError &) {}
    assert(names.count("a\"quote"));
    auto s = SymbolTableSource("p", vector<string>{"a\"quote", "good_sym"});
    // The raw quote must not appear between the generated literal's own
    // quotes, where it ends the string and breaks the C; the hostile name
    // is not a C identifier, so the table drops it entirely, the same rule
    // CheckExternSpec enforces on the names typecheck saw.
    assert(s.find("\"a\"quote\"") == string::npos);
    assert(s.find("void a") == string::npos);
    assert(s.find("void good_sym(void)") != string::npos);
    printf("ok\n");
    return 0;
}
