/* The one translation unit compiling the copied cute_spirv and ckit
   (third_party/cute_spirv), and the compiler's use of them: GLSL in, a gfx
   shader blob out (src/gfx/gfx_blob.h). See src/shaderc.h. */

#define CKIT_IMPLEMENTATION
#include "ckit.h"
#define CUTE_SPIRV_IMPLEMENTATION
#include "cute_spirv.h"

#include "shaderc.h"
#include "gfx/gfx_blob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *dir;              /* the shader's directory, with its separator */
    char **loaded;          /* include contents, freed after the compile */
    char **names;           /* and the paths errors show for them */
    int count, cap;
} shaderc_includes;

static char *shaderc_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    memcpy(d, s, n);
    return d;
}

static char *shaderc_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *s = (char *)malloc((size_t)n + 1);
    va_start(args, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, args);
    va_end(args);
    return s;
}

static char *shaderc_read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        if (n + 1 >= cap) buf = (char *)realloc(buf, cap *= 2);
        size_t got = fread(buf + n, 1, cap - n - 1, f);
        if (!got) break;
        n += got;
    }
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void shaderc_keep(shaderc_includes *inc, char *content, char *name) {
    if (inc->count == inc->cap) {
        inc->cap = inc->cap ? inc->cap * 2 : 8;
        inc->loaded = (char **)realloc(inc->loaded, sizeof(char *) * (size_t)inc->cap);
        inc->names = (char **)realloc(inc->names, sizeof(char *) * (size_t)inc->cap);
    }
    inc->loaded[inc->count] = content;
    inc->names[inc->count] = name;
    inc->count++;
}

static const char *shaderc_include(const char *path, void *user) {
    shaderc_includes *inc = (shaderc_includes *)user;
    char *full = shaderc_printf("%s%s", inc->dir, path);
    char *content = shaderc_read(full);
    if (!content) {
        free(full);
        return NULL;
    }
    shaderc_keep(inc, content, full);
    return content;
}

static const char *shaderc_display(const char *path, void *user) {
    shaderc_includes *inc = (shaderc_includes *)user;
    /* The include just resolved is the last one kept. */
    (void)path;
    return inc->count ? inc->names[inc->count - 1] : NULL;
}

static const char *stage_name(int stage) {
    return stage == GS_GFX_STAGE_VERTEX ? "vertex" : stage == GS_GFX_STAGE_FRAGMENT ? "fragment"
                                                                                   : "compute";
}

/* One class of resources, which SDL_GPU numbers consecutively within a set. */
typedef struct {
    const char *what;
    int set, first;         /* where the class must start */
    int count;
} shaderc_class;

/* Checks that the resources of `r` (n of them) in class `c` occupy bindings
   first..first+count-1 of set c->set, each exactly once. This is the
   contract SDL_GPU's backends number resources by; getting it wrong
   otherwise shows up only as a wrong or missing binding on one backend. */
static char *check_class(const char *path, int stage, const shaderc_class *c,
                         const CSPV_ReflectionResource *r, int n, const char *rule) {
    unsigned char seen[64] = { 0 };
    for (int i = 0; i < n; i++) {
        if (r[i].set != c->set)
            return shaderc_printf("%s: %s shader %s '%s' is in set %d, and must be in set %d (%s)",
                                  path, stage_name(stage), c->what, r[i].name, r[i].set, c->set,
                                  rule);
        int b = r[i].binding - c->first;
        if (b < 0 || b >= c->count || b >= 64 || seen[b])
            return shaderc_printf("%s: %s shader %s '%s' is at binding %d; the %d %s(s) of this "
                                  "shader must take bindings %d to %d of set %d (%s)",
                                  path, stage_name(stage), c->what, r[i].name, r[i].binding,
                                  c->count, c->what, c->first, c->first + c->count - 1, c->set,
                                  rule);
        seen[b] = 1;
    }
    return NULL;
}

/* Splits storage resources into read-only and read-write. */
static int split_ro(const CSPV_ReflectionResource *all, int n, CSPV_ReflectionResource *ro,
                    CSPV_ReflectionResource *rw, int *nrw) {
    int nro = 0;
    *nrw = 0;
    for (int i = 0; i < n; i++) {
        if (all[i].readonly) ro[nro++] = all[i];
        else rw[(*nrw)++] = all[i];
    }
    return nro;
}

static char *check_bindings(const char *path, int stage, const CSPV_Reflection *rf) {
    int ns = (int)asize(rf->samplers), ni = (int)asize(rf->storage_images);
    int nb = (int)asize(rf->storage_buffers), nu = (int)asize(rf->uniform_blocks);
    if (ns > 64 || ni > 64 || nb > 64 || nu > 4)
        return shaderc_printf("%s: too many resources for SDL_GPU (at most 4 uniform blocks per "
                              "stage)", path);
    CSPV_ReflectionResource iro[64], irw[64], bro[64], brw[64];
    int nirw, nbrw;
    int niro = split_ro(rf->storage_images, ni, iro, irw, &nirw);
    int nbro = split_ro(rf->storage_buffers, nb, bro, brw, &nbrw);
    CSPV_ReflectionResource ub[4];
    for (int i = 0; i < nu; i++) {
        ub[i].name = rf->uniform_blocks[i].name;
        ub[i].set = rf->uniform_blocks[i].set;
        ub[i].binding = rf->uniform_blocks[i].binding;
        ub[i].readonly = true;
    }
    char *err = NULL;
    if (stage == GS_GFX_STAGE_COMPUTE) {
        const char *rule = "SDL_GPU compute sets: 0 holds samplers, then read-only storage "
                           "textures, then read-only storage buffers; 1 read-write storage "
                           "textures, then read-write storage buffers; 2 uniform blocks";
        shaderc_class smp = { "sampler", 0, 0, ns };
        shaderc_class tro = { "read-only storage texture", 0, ns, niro };
        shaderc_class bro_c = { "read-only storage buffer", 0, ns + niro, nbro };
        shaderc_class trw = { "read-write storage texture", 1, 0, nirw };
        shaderc_class brw_c = { "read-write storage buffer", 1, nirw, nbrw };
        shaderc_class uni = { "uniform block", 2, 0, nu };
        if (!err) err = check_class(path, stage, &smp, rf->samplers, ns, rule);
        if (!err) err = check_class(path, stage, &tro, iro, niro, rule);
        if (!err) err = check_class(path, stage, &bro_c, bro, nbro, rule);
        if (!err) err = check_class(path, stage, &trw, irw, nirw, rule);
        if (!err) err = check_class(path, stage, &brw_c, brw, nbrw, rule);
        if (!err) err = check_class(path, stage, &uni, ub, nu, rule);
        return err;
    }
    int rs = stage == GS_GFX_STAGE_VERTEX ? 0 : 2;
    const char *rule = stage == GS_GFX_STAGE_VERTEX
        ? "SDL_GPU vertex sets: 0 holds samplers, then storage textures, then storage "
          "buffers; 1 uniform blocks"
        : "SDL_GPU fragment sets: 2 holds samplers, then storage textures, then storage "
          "buffers; 3 uniform blocks";
    if (nirw || nbrw) {
        const char *name = nirw ? irw[0].name : brw[0].name;
        return shaderc_printf("%s: %s shader storage %s '%s' must be declared readonly: SDL_GPU "
                              "graphics stages only read storage resources",
                              path, stage_name(stage), nirw ? "texture" : "buffer", name);
    }
    shaderc_class smp = { "sampler", rs, 0, ns };
    shaderc_class tex = { "storage texture", rs, ns, niro };
    shaderc_class buf = { "storage buffer", rs, ns + niro, nbro };
    shaderc_class uni = { "uniform block", rs + 1, 0, nu };
    if (!err) err = check_class(path, stage, &smp, rf->samplers, ns, rule);
    if (!err) err = check_class(path, stage, &tex, iro, niro, rule);
    if (!err) err = check_class(path, stage, &buf, bro, nbro, rule);
    if (!err) err = check_class(path, stage, &uni, ub, nu, rule);
    return err;
}

/* The blob, per src/gfx/gfx_blob.h. */
static unsigned char *write_blob(int stage, const CSPV_Result *r, size_t *size) {
    const CSPV_Reflection *rf = &r->reflection;
    gs_gfx_blob_header h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, GS_GFX_BLOB_MAGIC, 4);
    h.version = GS_GFX_BLOB_VERSION;
    h.stage = (uint8_t)stage;
    int ninputs = (int)asize(rf->inputs);
    h.num_inputs = (uint8_t)ninputs;
    for (int i = 0; i < 3; i++) h.local_size[i] = (uint32_t)rf->local_size[i];
    h.num_samplers = (uint16_t)asize(rf->samplers);
    for (int i = 0; i < (int)asize(rf->storage_images); i++) {
        if (rf->storage_images[i].readonly) h.num_storage_textures_ro++;
        else h.num_storage_textures_rw++;
    }
    for (int i = 0; i < (int)asize(rf->storage_buffers); i++) {
        if (rf->storage_buffers[i].readonly) h.num_storage_buffers_ro++;
        else h.num_storage_buffers_rw++;
    }
    int nu = (int)asize(rf->uniform_blocks);
    h.num_uniform_buffers = (uint16_t)nu;
    size_t msl_len = strlen(r->msl), hlsl_len = strlen(r->hlsl);
    size_t off = sizeof h + 4 * (size_t)nu + sizeof(gs_gfx_blob_input) * (size_t)ninputs;
    off = (off + 3) & ~(size_t)3;
    h.spirv_offset = (uint32_t)off;
    h.spirv_size = (uint32_t)(r->word_count * 4);
    off += h.spirv_size;
    h.msl_offset = (uint32_t)off;
    h.msl_size = (uint32_t)msl_len;
    off += msl_len + 1;
    h.hlsl_offset = (uint32_t)off;
    h.hlsl_size = (uint32_t)hlsl_len;
    off += hlsl_len + 1;
    unsigned char *blob = (unsigned char *)calloc(1, off);
    memcpy(blob, &h, sizeof h);
    unsigned char *p = blob + sizeof h;
    for (int b = 0; b < nu; b++) {
        /* By binding, which the check above made 0..nu-1. */
        for (int i = 0; i < nu; i++) {
            if (rf->uniform_blocks[i].binding != b) continue;
            uint32_t sz = (uint32_t)rf->uniform_blocks[i].size;
            memcpy(p, &sz, 4);
        }
        p += 4;
    }
    for (int i = 0; i < ninputs; i++) {
        gs_gfx_blob_input in;
        in.location = (uint8_t)rf->inputs[i].location;
        in.type = (uint8_t)rf->inputs[i].type;
        memcpy(p, &in, sizeof in);
        p += sizeof in;
    }
    memcpy(blob + h.spirv_offset, r->spirv, h.spirv_size);
    memcpy(blob + h.msl_offset, r->msl, msl_len);
    memcpy(blob + h.hlsl_offset, r->hlsl, hlsl_len);
    *size = off;
    return blob;
}

static char *check_inputs(const char *path, const CSPV_Reflection *rf) {
    unsigned char used[256] = { 0 };
    for (int i = 0; i < (int)asize(rf->inputs); i++) {
        const CSPV_ReflectionInput *in = &rf->inputs[i];
        int span = in->type == CSPV_TYPE_MAT4 ? 4 : 1;
        if (in->location < 0 || in->location + span > 16)
            return shaderc_printf("%s: vertex input '%s' needs a location from 0 to 15 "
                                  "(layout(location = N))", path, in->name);
        if (in->type == CSPV_TYPE_UNKNOWN)
            return shaderc_printf("%s: vertex input '%s' has a type a vertex buffer cannot "
                                  "supply", path, in->name);
        for (int k = 0; k < span; k++) {
            if (used[in->location + k])
                return shaderc_printf("%s: vertex input '%s' overlaps location %d",
                                      path, in->name, in->location + k);
            used[in->location + k] = 1;
        }
    }
    return NULL;
}

gs_shaderc_result gs_shaderc_compile(const char *source, const char *path, int stage) {
    gs_shaderc_result res;
    memset(&res, 0, sizeof res);
    shaderc_includes inc;
    memset(&inc, 0, sizeof inc);
    inc.dir = shaderc_strdup(path);
    char *slash = NULL;
    for (char *c = inc.dir; *c; c++)
        if (*c == '/' || *c == '\\') slash = c;
    if (slash) slash[1] = 0;
    else inc.dir[0] = 0;

    CSPV_Options opts;
    memset(&opts, 0, sizeof opts);
    opts.include_resolve = shaderc_include;
    opts.display_name = shaderc_display;
    opts.user = &inc;
    opts.emit_hlsl = true;
    opts.emit_msl = true;
    CSPV_Stage cs = stage == GS_GFX_STAGE_VERTEX ? CSPV_STAGE_VERTEX
                  : stage == GS_GFX_STAGE_FRAGMENT ? CSPV_STAGE_FRAGMENT : CSPV_STAGE_COMPUTE;
    CSPV_Result r = cspv_compile_ex(source, cs, &opts);
    if (!r.success) {
        /* cute_spirv calls the file it was handed "shader". */
        const char *msg = r.error_message ? r.error_message : "shader: error: compilation failed";
        if (!strncmp(msg, "shader:", 7)) res.error = shaderc_printf("%s:%s", path, msg + 7);
        else res.error = shaderc_strdup(msg);
    } else {
        res.error = check_bindings(path, stage, &r.reflection);
        if (!res.error && stage == GS_GFX_STAGE_VERTEX) res.error = check_inputs(path, &r.reflection);
        if (!res.error) res.blob = write_blob(stage, &r, &res.size);
    }
    cspv_free(&r);
    for (int i = 0; i < inc.count; i++) {
        free(inc.loaded[i]);
        free(inc.names[i]);
    }
    free(inc.loaded);
    free(inc.names);
    free(inc.dir);
    return res;
}

void gs_shaderc_free(gs_shaderc_result *r) {
    free(r->blob);
    free(r->error);
    r->blob = NULL;
    r->error = NULL;
}
