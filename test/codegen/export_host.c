/* The C host of export.goose, built by run_tests.py with the C the compiler
   writes for the program (--header): goose_init runs main, and then each
   export is called through its wrapper, the way C calls it. With the
   argument "early", one is called before goose_init, which is a runtime
   error. */
#include "export.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    /* The runtime makes stdout unbuffered as it starts; this host's lines
       are to order with the program's from the first. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "early")) {
        puts("early call");
        c_add(1, 2);
        puts("not reached");
        return 0;
    }
    goose_init(argc, argv);
    gs_i64 len = 0;
    const uint8_t *hello = hello_world(&len);
    printf("c: %.*s %d\n", (int)len, (const char *)hello, (int)c_add(20, 22));
    gs_i64 xs[4] = { 3, 1, 4, 1 };
    sl_i64 all = { xs, 4 }, mid = { xs + 1, 2 };
    printf("c: %lld %lld\n", (long long)c_weigh(all, 1), (long long)c_weigh(mid, 0));
    Wide_g w;
    memset(&w, 0, sizeof w);
    w.a_g = 5;
    Wide_g v = c_widen(w, 2);
    printf("c: %lld %lld\n", (long long)v.a_g, (long long)v.b_g);
    w.a_g = 3;
    w.b_g = 4;
    printf("c: %lld\n", (long long)c_peek_wide(w));
    goose_exit();
    puts("host done");
    return 0;
}
