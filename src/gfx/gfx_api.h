/* The gfx layer's C API: every function stdlib/gfx.goose reaches through an
   `extern "gs_gfx_..." fn`, listed once in GS_GFX_API. That list expands into
   the prototypes below, into the symbol table a JIT run registers
   (src/jit.h), and the test suite checks stdlib/gfx.goose against it.

   What crosses is what `extern fn` can pass (spec 7.10): scalars, `u8[:]`
   and slices of flat structs as { data, len }, flat structs by value, and T&
   as a pointer. A bool is a uint8_t. Goose structs are packed, so every
   struct here is packed too, and each is declared again in stdlib/gfx.goose
   with the same fields in the same order: the Goose struct `TextureDesc` is
   `gs_gfx_texture_desc` here, a slice of `Buffer` is `gs_gfx_buffer_slice`.

   Handles are one u32: a slot index in the low 20 bits and a generation
   above it, so a released or stale handle is an error, not a crash. 0 is
   never a valid handle.

   Errors: a function that can fail for reasons outside the program (no GPU,
   a file that is not there) returns false or a zero handle, with the reason
   in gs_gfx_error. A call the program should not have made (drawing with no
   pipeline bound, a stale handle) is also counted by gs_gfx_misuse_count,
   which the Goose side checks every frame and turns into an abort. */

#ifndef GS_GFX_API_H
#define GS_GFX_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

typedef struct { uint8_t *data; int64_t len; } gs_gfx_bytes;   /* u8[:], the generated sl_u8 */

typedef struct { uint32_t id; } gs_gfx_buffer;
typedef struct { uint32_t id; } gs_gfx_texture;
typedef struct { uint32_t id; } gs_gfx_sampler;
typedef struct { uint32_t id; } gs_gfx_pipeline;
typedef struct { uint32_t id; } gs_gfx_compute_pipeline;

typedef struct { gs_gfx_buffer *data; int64_t len; } gs_gfx_buffer_slice;
typedef struct { gs_gfx_texture *data; int64_t len; } gs_gfx_texture_slice;

typedef struct { int32_t x, y; } gs_gfx_int2;
typedef struct { float x, y; } gs_gfx_float2;
typedef struct { float x, y, z, w; } gs_gfx_float4;

typedef struct {
    int32_t kind, format, usage;
    int32_t width, height;
    int32_t depth;              /* a 3D texture's depth, or an array's layers */
    int32_t mips;               /* 0: the full chain */
    int32_t samples;
} gs_gfx_texture_desc;

/* Part of one mip of one layer. w == 0 means the whole mip. */
typedef struct {
    int32_t mip, layer;
    int32_t x, y, z;
    int32_t w, h, d;
} gs_gfx_region;

typedef struct {
    int32_t min_filter, mag_filter, mip_filter;
    int32_t wrap_u, wrap_v, wrap_w;
    float max_anisotropy;       /* 1 or less: off */
    int32_t compare;            /* a COMPARE_*, for a shadow sampler; 0: none */
} gs_gfx_sampler_desc;

typedef struct {
    int32_t primitive, cull;
    uint8_t clockwise;          /* front faces wind clockwise */
    uint8_t wireframe, depth_test, depth_write;
    int32_t depth_compare, blend;
    /* Vertex inputs at this location and above come from vertex buffer 1,
       per instance; -1: none do. */
    int32_t instance_location;
    float depth_bias, depth_bias_slope;
    /* Per location, the attribute format a vertex buffer supplies; 0: the
       shader input's own type, as f32/i32/u32 components. */
    uint8_t formats[16];
} gs_gfx_pipeline_desc;

typedef struct {
    gs_gfx_texture color[4];    /* the targets, up to the first 0 */
    gs_gfx_texture resolve[4];  /* for a multisampled target, where it resolves to */
    gs_gfx_texture depth;       /* 0: none */
    int32_t layer, mip;         /* of the color targets */
    uint8_t clear_color, clear_depth;
    gs_gfx_float4 color_value;
    float depth_value;
} gs_gfx_pass_desc;

#pragma pack(pop)

#define GS_GFX_API(X) \
    /* Device, window and frames. */ \
    X(uint8_t, gs_gfx_available, (void)) \
    X(uint8_t, gs_gfx_open, (gs_gfx_bytes title, int64_t width, int64_t height, int64_t flags)) \
    X(uint8_t, gs_gfx_open_headless, (int64_t width, int64_t height, int64_t flags)) \
    X(void, gs_gfx_close, (void)) \
    X(int64_t, gs_gfx_error, (gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_misuse_count, (void)) \
    X(int64_t, gs_gfx_driver, (gs_gfx_bytes out)) \
    X(uint8_t, gs_gfx_frame, (void)) \
    X(void, gs_gfx_quit, (void)) \
    X(void, gs_gfx_flush, (void)) \
    X(void, gs_gfx_set_title, (gs_gfx_bytes title)) \
    X(void, gs_gfx_screen_size, (gs_gfx_int2 *out)) \
    X(gs_gfx_texture, gs_gfx_screen, (void)) \
    X(gs_gfx_texture, gs_gfx_screen_depth, (void)) \
    X(double, gs_gfx_time, (void)) \
    X(double, gs_gfx_delta_time, (void)) \
    X(int64_t, gs_gfx_frame_count, (void)) \
    /* Input. */ \
    X(uint8_t, gs_gfx_key_down, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_key_pressed, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_key_released, (gs_gfx_bytes name)) \
    X(uint8_t, gs_gfx_mouse_down, (int64_t button)) \
    X(uint8_t, gs_gfx_mouse_pressed, (int64_t button)) \
    X(uint8_t, gs_gfx_mouse_released, (int64_t button)) \
    X(void, gs_gfx_mouse_pos, (gs_gfx_float2 *out)) \
    X(void, gs_gfx_mouse_delta, (gs_gfx_float2 *out)) \
    X(float, gs_gfx_mouse_wheel, (void)) \
    X(uint8_t, gs_gfx_inject_key, (gs_gfx_bytes name, uint8_t down)) \
    X(void, gs_gfx_inject_mouse, (float x, float y, int64_t button, uint8_t down)) \
    /* Buffers. */ \
    X(gs_gfx_buffer, gs_gfx_create_buffer, (int64_t usage, int64_t size, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_update_buffer, (gs_gfx_buffer b, int64_t offset, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_read_buffer, (gs_gfx_buffer b, int64_t offset, gs_gfx_bytes out)) \
    X(int64_t, gs_gfx_buffer_size, (gs_gfx_buffer b)) \
    X(void, gs_gfx_release_buffer, (gs_gfx_buffer b)) \
    /* Textures and samplers. */ \
    X(gs_gfx_texture, gs_gfx_create_texture, (const gs_gfx_texture_desc *desc)) \
    X(gs_gfx_texture, gs_gfx_load_texture, (gs_gfx_bytes path, int64_t flags)) \
    X(uint8_t, gs_gfx_update_texture, (gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes data)) \
    X(uint8_t, gs_gfx_read_texture, (gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes out)) \
    X(uint8_t, gs_gfx_save_png, (gs_gfx_texture t, gs_gfx_bytes path)) \
    X(void, gs_gfx_generate_mips, (gs_gfx_texture t)) \
    X(uint8_t, gs_gfx_texture_info, (gs_gfx_texture t, gs_gfx_texture_desc *out)) \
    X(void, gs_gfx_release_texture, (gs_gfx_texture t)) \
    X(gs_gfx_sampler, gs_gfx_create_sampler, (const gs_gfx_sampler_desc *desc)) \
    X(void, gs_gfx_release_sampler, (gs_gfx_sampler s)) \
    /* Pipelines. */ \
    X(gs_gfx_pipeline, gs_gfx_create_pipeline, (gs_gfx_bytes vs, gs_gfx_bytes fs, const gs_gfx_pipeline_desc *desc)) \
    X(void, gs_gfx_release_pipeline, (gs_gfx_pipeline p)) \
    X(gs_gfx_compute_pipeline, gs_gfx_create_compute_pipeline, (gs_gfx_bytes cs)) \
    X(void, gs_gfx_release_compute_pipeline, (gs_gfx_compute_pipeline p)) \
    /* Render passes and drawing. */ \
    X(uint8_t, gs_gfx_begin_pass, (const gs_gfx_pass_desc *desc)) \
    X(void, gs_gfx_end_pass, (void)) \
    X(void, gs_gfx_bind_pipeline, (gs_gfx_pipeline p)) \
    X(void, gs_gfx_bind_vertex_buffer, (int64_t slot, gs_gfx_buffer b, int64_t offset)) \
    X(void, gs_gfx_bind_index_buffer, (gs_gfx_buffer b, int64_t index_size, int64_t offset)) \
    X(void, gs_gfx_bind_texture, (int64_t stage, int64_t slot, gs_gfx_texture t, gs_gfx_sampler s)) \
    X(void, gs_gfx_bind_storage_texture, (int64_t stage, int64_t slot, gs_gfx_texture t)) \
    X(void, gs_gfx_bind_storage_buffer, (int64_t stage, int64_t slot, gs_gfx_buffer b)) \
    X(void, gs_gfx_push_uniforms, (int64_t stage, int64_t slot, gs_gfx_bytes data)) \
    X(void, gs_gfx_viewport, (float x, float y, float w, float h)) \
    X(void, gs_gfx_scissor, (int64_t x, int64_t y, int64_t w, int64_t h)) \
    X(void, gs_gfx_draw, (int64_t vertices, int64_t instances, int64_t first_vertex, int64_t first_instance)) \
    X(void, gs_gfx_draw_indexed, (int64_t indices, int64_t instances, int64_t first_index, int64_t vertex_offset, int64_t first_instance)) \
    /* Compute. */ \
    X(uint8_t, gs_gfx_begin_compute, (gs_gfx_buffer_slice rw_buffers, gs_gfx_texture_slice rw_textures)) \
    X(void, gs_gfx_end_compute, (void)) \
    X(void, gs_gfx_bind_compute_pipeline, (gs_gfx_compute_pipeline p)) \
    X(void, gs_gfx_dispatch, (int64_t x, int64_t y, int64_t z))

#define GS_GFX_PROTO(ret, name, params) ret name params;
GS_GFX_API(GS_GFX_PROTO)
#undef GS_GFX_PROTO

/* The constants stdlib/gfx.goose declares, with their Goose types; here they
   are GS_GFX_<name>. Where the layer passes one straight through to SDL, it
   has SDL's value, which SDL keeps stable. */
#define GS_GFX_CONSTANTS(X) \
    /* open() and open_headless() flags. */ \
    X(i32, WINDOW_RESIZABLE, 1) \
    X(i32, WINDOW_HIDDEN, 2) \
    X(i32, WINDOW_FULLSCREEN, 4) \
    X(i32, WINDOW_HIGH_DPI, 8) \
    X(i32, NO_VSYNC, 16) \
    X(i32, DEBUG, 32) \
    /* Shader stages. */ \
    X(i32, VERTEX, 0) \
    X(i32, FRAGMENT, 1) \
    X(i32, COMPUTE, 2) \
    /* Buffer usage, SDL_GPUBufferUsageFlags. */ \
    X(i32, BUFFER_VERTEX, 1) \
    X(i32, BUFFER_INDEX, 2) \
    X(i32, BUFFER_INDIRECT, 4) \
    X(i32, BUFFER_STORAGE, 8) \
    X(i32, BUFFER_COMPUTE_READ, 16) \
    X(i32, BUFFER_COMPUTE_WRITE, 32) \
    /* Texture kinds, SDL_GPUTextureType. */ \
    X(i32, TEXTURE_2D, 0) \
    X(i32, TEXTURE_2D_ARRAY, 1) \
    X(i32, TEXTURE_3D, 2) \
    X(i32, TEXTURE_CUBE, 3) \
    /* Texture usage, SDL_GPUTextureUsageFlags. */ \
    X(i32, SAMPLED, 1) \
    X(i32, COLOR_TARGET, 2) \
    X(i32, DEPTH_TARGET, 4) \
    X(i32, STORAGE_READ, 8) \
    X(i32, COMPUTE_READ, 16) \
    X(i32, COMPUTE_WRITE, 32) \
    X(i32, COMPUTE_READ_WRITE, 64) \
    /* Texture formats. DEPTH is the best supported of DEPTH32F and DEPTH24. */ \
    X(i32, RGBA8, 1) \
    X(i32, BGRA8, 2) \
    X(i32, RGBA8_SRGB, 3) \
    X(i32, R8, 4) \
    X(i32, RG8, 5) \
    X(i32, RGBA16F, 6) \
    X(i32, RGBA32F, 7) \
    X(i32, R16F, 8) \
    X(i32, RG16F, 9) \
    X(i32, R32F, 10) \
    X(i32, RG32F, 11) \
    X(i32, R32UI, 12) \
    X(i32, RGBA8UI, 13) \
    X(i32, RGB10A2, 14) \
    X(i32, RG11B10F, 15) \
    X(i32, DEPTH16, 16) \
    X(i32, DEPTH24, 17) \
    X(i32, DEPTH32F, 18) \
    X(i32, DEPTH24_STENCIL8, 19) \
    X(i32, DEPTH, 20) \
    /* Samplers: SDL_GPUFilter, SDL_GPUSamplerAddressMode, SDL_GPUCompareOp. */ \
    X(i32, NEAREST, 0) \
    X(i32, LINEAR, 1) \
    X(i32, REPEAT, 0) \
    X(i32, MIRROR, 1) \
    X(i32, CLAMP, 2) \
    X(i32, COMPARE_NEVER, 1) \
    X(i32, COMPARE_LESS, 2) \
    X(i32, COMPARE_EQUAL, 3) \
    X(i32, COMPARE_LESS_EQUAL, 4) \
    X(i32, COMPARE_GREATER, 5) \
    X(i32, COMPARE_NOT_EQUAL, 6) \
    X(i32, COMPARE_GREATER_EQUAL, 7) \
    X(i32, COMPARE_ALWAYS, 8) \
    /* Pipelines: SDL_GPUPrimitiveType, SDL_GPUCullMode, then blend modes. */ \
    X(i32, TRIANGLES, 0) \
    X(i32, TRIANGLE_STRIP, 1) \
    X(i32, LINES, 2) \
    X(i32, LINE_STRIP, 3) \
    X(i32, POINTS, 4) \
    X(i32, CULL_NONE, 0) \
    X(i32, CULL_FRONT, 1) \
    X(i32, CULL_BACK, 2) \
    X(i32, BLEND_NONE, 0) \
    X(i32, BLEND_ALPHA, 1) \
    X(i32, BLEND_ADD, 2) \
    X(i32, BLEND_PREMULTIPLIED, 3) \
    X(i32, BLEND_MULTIPLY, 4) \
    /* Vertex attribute formats, for PipelineDesc.formats. */ \
    X(u8, VERTEX_FLOAT, 1) \
    X(u8, VERTEX_FLOAT2, 2) \
    X(u8, VERTEX_FLOAT3, 3) \
    X(u8, VERTEX_FLOAT4, 4) \
    X(u8, VERTEX_INT, 5) \
    X(u8, VERTEX_INT2, 6) \
    X(u8, VERTEX_INT3, 7) \
    X(u8, VERTEX_INT4, 8) \
    X(u8, VERTEX_UINT, 9) \
    X(u8, VERTEX_UINT2, 10) \
    X(u8, VERTEX_UINT3, 11) \
    X(u8, VERTEX_UINT4, 12) \
    X(u8, VERTEX_UBYTE4_NORM, 13) \
    X(u8, VERTEX_BYTE4_NORM, 14) \
    X(u8, VERTEX_UBYTE4, 15) \
    X(u8, VERTEX_SHORT2, 16) \
    X(u8, VERTEX_SHORT4, 17) \
    X(u8, VERTEX_USHORT2_NORM, 18) \
    X(u8, VERTEX_USHORT4_NORM, 19) \
    X(u8, VERTEX_HALF2, 20) \
    X(u8, VERTEX_HALF4, 21) \
    /* load_texture() flags. */ \
    X(i32, LOAD_MIPS, 1) \
    X(i32, LOAD_SRGB, 2) \
    /* Mouse buttons. */ \
    X(i32, MOUSE_LEFT, 1) \
    X(i32, MOUSE_MIDDLE, 2) \
    X(i32, MOUSE_RIGHT, 3)

#define GS_GFX_ENUM(type, name, value) GS_GFX_##name = value,
enum { GS_GFX_CONSTANTS(GS_GFX_ENUM) };
#undef GS_GFX_ENUM

#ifdef __cplusplus
}
#endif

#endif
