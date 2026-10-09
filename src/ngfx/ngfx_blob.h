/* What embed_slang embeds (src/slangc.h) and the ngfx layer reads: one Slang
   module, every entry point of it, compiled for both of NoGraphicsAPI's
   backends. Little-endian, 4-byte fields:

     header | entries[entry_count] | SPIR-V | metallib

   The SPIR-V is always there; the metallib is empty when the program was
   compiled off a Mac, and the layer then fails to make a pipeline on Metal,
   saying so. The
   SPIR-V holds every entry point in one module, and the metallib every
   function, so a stage is the shared code plus its entry name, as
   NoGraphicsAPI's ShaderStage is. The blob sits in the program's static data
   with no particular alignment, so the layer copies the code out. */

#ifndef GS_NGFX_BLOB_H
#define GS_NGFX_BLOB_H

#include <stdint.h>

#define GS_NGFX_BLOB_MAGIC 0x42534e47u /* "GNSB" */
#define GS_NGFX_BLOB_VERSION 1u
#define GS_NGFX_BLOB_NAME_SIZE 56

enum {
    GS_NGFX_BLOB_STAGE_VERTEX = 0,
    GS_NGFX_BLOB_STAGE_FRAGMENT = 1,
    GS_NGFX_BLOB_STAGE_COMPUTE = 2,
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t spirv_offset;     /* from the start of the blob */
    uint32_t spirv_size;
    uint32_t metallib_offset;
    uint32_t metallib_size;
    uint32_t reserved;
} gs_ngfx_blob_header;

typedef struct {
    char name[GS_NGFX_BLOB_NAME_SIZE];  /* NUL-terminated */
    uint32_t stage;                     /* GS_NGFX_BLOB_STAGE_* */
    uint32_t threadgroup[3];            /* numthreads; 1 x 1 x 1 for graphics stages */
} gs_ngfx_blob_entry;

#endif
