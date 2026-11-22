/* The C host of bce_export_entry.goose, built by run_tests.py with the C the
   compiler writes for the program (--header): goose_init runs main, and
   then the export is called through its wrapper with an index past the
   end, which its kept bounds check stops. */
#include "bce_export_entry.h"
#include <stdio.h>

int main(int argc, char **argv) {
    /* The runtime makes stdout unbuffered as it starts; this host's lines
       are to order with the program's from the first. */
    setvbuf(stdout, NULL, _IONBF, 0);
    goose_init(argc, argv);
    gs_i32 d[1] = { 7 };
    sl_i32 s = { d, 1 };
    printf("c: %d\n", (int)c_pick(s, 0));
    c_pick(s, 1);
    puts("not reached");
    return 0;
}
