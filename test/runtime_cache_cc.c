/* A controlled C compiler for runtime_cache.py: leave an unfinished object
   until the test releases it, or fail after writing one. A link accepts only
   completed objects. Supports both driver flag styles on every platform. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(text, f);
    return fclose(f) == 0;
}

static int marker_exists(const char *name) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", getenv("GOOSE_CACHE_TEST"), name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--version"))) {
        puts("cache compiler version 1.0");
        return 0;
    }
    const char *out = NULL, *object = NULL;
    int compile = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "/c")) compile = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strncmp(argv[i], "/Fo:", 4) || !strncmp(argv[i], "/Fe:", 4)) {
            if (!out) out = argv[i] + 4;
        } else if (strstr(argv[i], "goose_runtime.")) object = argv[i];
    }
    if (!out) return 2;
    if (compile) {
        if (!write_text(out, "partial")) return 3;
        char marker[4096];
        #ifdef _WIN32
            unsigned long pid = GetCurrentProcessId();
        #else
            unsigned long pid = (unsigned long)getpid();
        #endif
        snprintf(marker, sizeof marker, "%s/started-%lu", getenv("GOOSE_CACHE_TEST"), pid);
        if (!write_text(marker, out)) return 4;
        if (marker_exists("fail")) {
            fputs("intentional runtime compile failure\n", stderr);
            return 5;
        }
        for (int tries = 0; !marker_exists("release"); ++tries) {
            if (tries == 3000) return 6;
            #ifdef _WIN32
                Sleep(10);
            #else
                usleep(10000);
            #endif
        }
        return write_text(out, "complete") ? 0 : 7;
    }
    char contents[16] = {0};
    FILE *f = object ? fopen(object, "rb") : NULL;
    if (f) {
        fread(contents, 1, sizeof contents - 1, f);
        fclose(f);
    }
    if (strcmp(contents, "complete")) {
        fputs("linked an incomplete runtime object\n", stderr);
        return 8;
    }
    return write_text(out, "linked") ? 0 : 9;
}
