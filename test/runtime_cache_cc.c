/* A controlled C compiler for runtime_cache.py and native_cache.py: leave an
   unfinished object until the test releases it, or fail after writing one.
   A link accepts only completed objects, the runtime's and those its
   response files list. Supports both driver flag styles on every platform. */
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

static int complete(const char *object) {
    char contents[16] = {0};
    FILE *f = object ? fopen(object, "rb") : NULL;
    if (f) {
        fread(contents, 1, sizeof contents - 1, f);
        fclose(f);
    }
    return !strcmp(contents, "complete");
}

/* Every object a response file lists, a line each, quoted or not. */
static int listed_complete(const char *rsp) {
    char line[4096];
    int ok = 1;
    FILE *f = fopen(rsp, "rb");
    if (!f) return 0;
    while (ok && fgets(line, sizeof line, f)) {
        char *item = line;
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        if (n > 1 && line[0] == '"' && line[n - 1] == '"') {
            line[n - 1] = 0;
            item++;
            n -= 2;
        }
        if ((n > 2 && !strcmp(item + n - 2, ".o")) || (n > 4 && !strcmp(item + n - 4, ".obj")))
            ok = complete(item);
    }
    fclose(f);
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--version"))) {
        puts("cache compiler version 1.0");
        return 0;
    }
    const char *out = NULL, *object = NULL, *rsps[16];
    int compile = 0, nrsps = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "/c")) compile = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strncmp(argv[i], "/Fo:", 4) || !strncmp(argv[i], "/Fe:", 4)) {
            if (!out) out = argv[i] + 4;
        } else if (argv[i][0] == '@' && nrsps < 16) rsps[nrsps++] = argv[i] + 1;
        else if (strstr(argv[i], "goose_runtime.")) object = argv[i];
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
    if (!complete(object)) {
        fputs("linked an incomplete runtime object\n", stderr);
        return 8;
    }
    for (int i = 0; i < nrsps; ++i)
        if (!listed_complete(rsps[i])) {
            fprintf(stderr, "linked an incomplete object that %s lists\n", rsps[i]);
            return 10;
        }
    return write_text(out, "linked") ? 0 : 9;
}
