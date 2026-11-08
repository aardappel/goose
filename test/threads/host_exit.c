/* The C host of host_exit.goose, built by run_tests.py with the C the
   compiler writes for the program (--header): the program is started, used
   and ended three times over, each round finding nothing of the last (main's
   line shows the globals fresh, and the queue is empty again); with the
   argument "live", it is ended while a worker of it still runs, which is a
   runtime error. */
#include "host_exit.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int live = argc > 1 && !strcmp(argv[1], "live");
    /* The runtime makes stdout unbuffered as it starts; this host's lines
       are to order with the program's from the first. */
    setvbuf(stdout, NULL, _IONBF, 0);
    goose_exit();   /* nothing is started yet: it does nothing */
    for (int round = 0; round < 3; round++) {
        goose_init(argc, argv);
        goose_init(argc, argv);   /* a second call does nothing */
        long long a = (long long)bump();
        long long b = (long long)bump();
        printf("round %d: %lld %lld\n", round, a, b);
        if (live && round == 2) park();
        goose_exit();
    }
    puts("host done");
    return 0;
}
