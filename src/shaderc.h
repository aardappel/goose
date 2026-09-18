/* The compiler's shader compiler: GLSL in, a gfx shader blob out
   (src/gfx/gfx_blob.h). Implemented in C by src/shaderc.c over the copied
   third_party/cute_spirv; always built, with or without SDL, so that a
   program embedding shaders typechecks and emits C the same either way. */

#ifndef GS_SHADERC_H
#define GS_SHADERC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *blob;   /* malloc'd; set on success */
    size_t size;
    char *error;           /* malloc'd; set on failure, "path:line: error: ..." */
} gs_shaderc_result;

/* Compiles `source` for `stage` (GS_GFX_STAGE_*). `path` names the file in
   error messages, and #include resolves relative to its directory. */
gs_shaderc_result gs_shaderc_compile(const char *source, const char *path, int stage);
void gs_shaderc_free(gs_shaderc_result *r);

#ifdef __cplusplus
}
#endif

#endif
