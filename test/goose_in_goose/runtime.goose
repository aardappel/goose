// Independently written target runtime. This text is emitted verbatim; it
// neither includes nor extracts the bootstrap compiler's runtime.
fn runtime_source() -> const u8[:] {
    return """
    /* Goose-in-Goose experimental C backend. Generated from test/goose_in_goose/. */
    #include <stdint.h>
    #include <stddef.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>
    #include <limits.h>
    #ifdef _MSC_VER
    #include <malloc.h>
    #endif
    #ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #else
    #include <sys/mman.h>
    #include <unistd.h>
    #endif
    typedef struct { void *data; int64_t len; } gg_slice;
    typedef struct { unsigned char *base; int64_t len; } gg_header;
    typedef struct { unsigned char *base; size_t top, committed; } gg_stack;
    typedef struct { gg_header *header; gg_stack *stack; } gg_reference;
    #define GG_RESERVATION ((size_t)268435456)
    #define GG_STACK_COUNT 8192
    static gg_stack gg_stacks[GG_STACK_COUNT];
    static int gg_argc;
    static char **gg_argv;
    static void gg_fail(const char *message) {
        fprintf(stderr, "goose runtime error: %s\n", message); exit(1);
    }
    static gg_stack *gg_get_stack(int64_t id) {
        gg_stack *s;
        if (id < 0 || id >= GG_STACK_COUNT) gg_fail("data stack limit exceeded");
        s = &gg_stacks[id];
        if (!s->base) {
    #ifdef _WIN32
            s->base = VirtualAlloc(NULL, GG_RESERVATION + 65536, MEM_RESERVE, PAGE_NOACCESS);
    #else
            s->base = mmap(NULL, GG_RESERVATION + 65536, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (s->base == MAP_FAILED) s->base = NULL;
    #endif
            if (!s->base) gg_fail("cannot reserve data stack");
        }
        return s;
    }
    static void *gg_allocate(gg_stack *s, size_t size) {
        size_t end, committed;
        void *result;
        if (size > GG_RESERVATION - s->top) gg_fail("data stack reservation exceeded");
        result = s->base + s->top; end = s->top + size;
        if (end > s->committed) {
            committed = (end + 65535) & ~(size_t)65535;
    #ifdef _WIN32
            if (!VirtualAlloc(s->base + s->committed, committed - s->committed, MEM_COMMIT, PAGE_READWRITE)) gg_fail("cannot commit data stack");
    #else
            if (mprotect(s->base + s->committed, committed - s->committed, PROT_READ | PROT_WRITE)) gg_fail("cannot commit data stack");
    #endif
            s->committed = committed;
        }
        s->top = end; return result;
    }
    static gg_reference gg_ref(gg_header *h, gg_stack *s) { gg_reference r; r.header=h; r.stack=s; return r; }
    static gg_slice gg_view(void *p, int64_t n) { gg_slice v; v.data=p; v.len=n; return v; }
    static gg_slice gg_sub(gg_slice a, int64_t lo, int64_t hi, size_t element) {
        if (lo < 0 || hi < lo || hi > a.len) gg_fail("slice out of bounds");
        return gg_view((unsigned char *)a.data + (size_t)lo * element, hi - lo);
    }
    static int64_t gg_index(int64_t i, int64_t len) { if ((uint64_t)i >= (uint64_t)len) gg_fail("array index out of bounds"); return i; }
    static void gg_append(gg_reference a, const void *p, int64_t n, size_t element) {
        void *to;
        if (n < 0 || (uint64_t)n > GG_RESERVATION / (element ? element : 1)) gg_fail("invalid array size");
        to=gg_allocate(a.stack, (size_t)n * element);
        if (n && element) memmove(to, p, (size_t)n * element);
        a.header->len += n;
    }
    static void gg_resize(gg_reference a, int64_t n, size_t element, const void *fill) {
        int64_t old=a.header->len, i;
        if (n < 0) gg_fail("negative array length");
        if (n > old) {
            unsigned char *p;
            if (!fill) gg_fail("resize without fill may only shrink");
            if ((uint64_t)(n-old) > GG_RESERVATION / (element ? element : 1)) gg_fail("invalid array size");
            p=gg_allocate(a.stack, (size_t)(n-old)*element);
            for (i=0; i<n-old; i++) if(element) memcpy(p+(size_t)i*element, fill, element);
        } else { a.stack->top = (size_t)(a.header->base-a.stack->base) + (size_t)n*element; }
        a.header->len=n;
    }
    static int64_t gg_div(int64_t a, int64_t b) { if (!b) gg_fail("division by zero"); if (a==INT64_MIN && b==-1) gg_fail("division overflow"); return a/b; }
    static int64_t gg_mod(int64_t a, int64_t b) {
        int64_t r; if (!b) gg_fail("division by zero"); if (b==-1) return 0;
        r=a%b; if(r<0) return (int64_t)((uint64_t)r + (b<0 ? (uint64_t)0-(uint64_t)b : (uint64_t)b)); return r;
    }
    static uint64_t gg_udiv(uint64_t a,uint64_t b) { if(!b)gg_fail("division by zero");return a/b; }
    static uint64_t gg_umod(uint64_t a,uint64_t b) { if(!b)gg_fail("division by zero");return a%b; }
    static int gg_equal(gg_slice a, gg_slice b, size_t element) { return a.len==b.len && (!a.len || !memcmp(a.data,b.data,(size_t)a.len*element)); }
    static void gg_text(gg_reference out, gg_slice v) { gg_append(out,v.data,v.len,1); }
    static void gg_integer(gg_reference out,int64_t n) { char b[32]; int k=snprintf(b,sizeof b,"%lld",(long long)n);gg_append(out,b,k,1); }
    static void gg_unsigned(gg_reference out,uint64_t n) { char b[32]; int k=snprintf(b,sizeof b,"%llu",(unsigned long long)n);gg_append(out,b,k,1); }
    static void gg_float(gg_reference out,double n) { char b[64];int k=snprintf(b,sizeof b,"%.15g",n);if(strtod(b,NULL)!=n)k=snprintf(b,sizeof b,"%.17g",n);gg_append(out,b,k,1); }
    static void gg_bool(gg_reference out,uint8_t n) { gg_append(out,n?"true":"false",n?4:5,1); }
    static void gg_write(FILE *file,gg_slice text) { if(text.len && fwrite(text.data,1,(size_t)text.len,file)!=(size_t)text.len)gg_fail("write failed"); }
    static char *gg_cstring(gg_slice path) {
        char *s=malloc((size_t)path.len+1); if(!s)gg_fail("out of memory in file shim");
        if(path.len)memcpy(s,path.data,(size_t)path.len);s[path.len]=0;return s;
    }
    static uint8_t gs_os_read_file(gg_slice path,gg_reference out) {
        char *p=gg_cstring(path);FILE *f=fopen(p,"rb");unsigned char b[16384];size_t n;int ok;free(p);
        if(!f)return 0;while((n=fread(b,1,sizeof b,f))!=0)gg_append(out,b,(int64_t)n,1);ok=!ferror(f);fclose(f);return (uint8_t)ok;
    }
    static uint8_t gs_os_write_file(gg_slice path,gg_slice data) {
        char *p=gg_cstring(path);FILE *f=fopen(p,"wb");int ok;free(p);if(!f)return 0;
        ok=!data.len || fwrite(data.data,1,(size_t)data.len,f)==(size_t)data.len;if(fclose(f))ok=0;return (uint8_t)ok;
    }
    static uint8_t gs_os_file_exists(gg_slice path) { char*p=gg_cstring(path);FILE*f=fopen(p,"rb");free(p);if(!f)return 0;fclose(f);return 1; }
    static void gs_os_write_stdout(gg_slice s) { gg_write(stdout,s); }
    static void gs_os_write_stderr(gg_slice s) { gg_write(stderr,s); }
    static void gs_os_flush_stdout(void) { fflush(stdout); }
    static int64_t gs_os_arg_count(void) { return gg_argc; }
    static void gs_os_arg(int64_t i,gg_reference out) { if(i>=0 && i<gg_argc)gg_append(out,gg_argv[i],(int64_t)strlen(gg_argv[i]),1); }
    """;
}
