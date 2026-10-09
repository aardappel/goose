/* The ngfx layer's C API: every function stdlib/ngfx.goose reaches through an
   `extern "gs_ngfx_..." fn`, listed once in GS_NGFX_API. That list expands
   into the prototypes below, into the symbol table a JIT run registers
   (src/jit.h), and the test suite checks stdlib/ngfx.goose against it.

   ngfx is gfx's platform half (window, input, time, the screen, textures and
   read-back) over NoGraphicsAPI, with NoGraphicsAPI's way of drawing: a draw
   takes the GPU address of a root struct the program fills, buffers are
   plain GPU memory with addresses, and textures and samplers are indices
   into heaps the layer owns (docs/design/ngfx.md).

   The conventions are gfx's (src/gfx/gfx_api.h): what crosses is what
   `extern fn` can pass; structs are packed and declared again in
   stdlib/ngfx.goose with the same fields; handles are one u32 with a
   generation, 0 never valid; failures outside the program return false or a
   zero handle with the reason in gs_ngfx_error, and misuse is counted for
   the Goose side to abort on. */

#ifndef GS_NGFX_API_H
#define GS_NGFX_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

typedef struct { uint8_t *data; int64_t len; } gs_ngfx_bytes;   /* u8[:] */

typedef struct { uint32_t id; } gs_ngfx_buffer;
typedef struct { uint32_t id; } gs_ngfx_texture;
typedef struct { uint32_t id; } gs_ngfx_sampler;
typedef struct { uint32_t id; } gs_ngfx_pipeline;
typedef struct { uint32_t id; } gs_ngfx_compute_pipeline;

/* One input event of a frame, as gfx's. */
typedef struct {
    int32_t kind;
    int32_t scancode;
    int32_t keycode;
    int32_t mods;
    uint8_t down;
    uint8_t repeat;
    int32_t button;
    int32_t clicks;
    uint32_t codepoint;
    float x, y;
} gs_ngfx_event;

typedef struct { gs_ngfx_event *data; int64_t len; } gs_ngfx_event_slice;

typedef struct { int32_t x, y; } gs_ngfx_int2;
typedef struct { float x, y; } gs_ngfx_float2;
typedef struct { float x, y, z, w; } gs_ngfx_float4;

typedef struct {
    int32_t kind, format, usage;
    int32_t width, height;
    int32_t depth;              /* a 3D texture's depth, or an array's layers */
    int32_t mips;               /* 0: the full chain */
} gs_ngfx_texture_desc;

/* Part of one mip of one layer. w == 0 means the whole mip. */
typedef struct {
    int32_t mip, layer;
    int32_t x, y, z;
    int32_t w, h, d;
} gs_ngfx_region;

typedef struct {
    int32_t min_filter, mag_filter, mip_filter;
    int32_t wrap_u, wrap_v, wrap_w;
    float max_anisotropy;       /* more than 1: NoGraphicsAPI's fixed 4x */
    int32_t compare;            /* a COMPARE_*, for a shadow sampler; 0: none */
} gs_ngfx_sampler_desc;

/* What a pipeline adds to its shaders. The formats it draws into come from
   the pass it is bound in: one NoGraphicsAPI PSO per set of them. */
typedef struct {
    int32_t cull;
    uint8_t clockwise;          /* front faces wind clockwise */
    uint8_t depth_test, depth_write;
    int32_t depth_compare, blend;
    float depth_bias, depth_bias_slope;
} gs_ngfx_pipeline_desc;

typedef struct {
    gs_ngfx_texture color[4];   /* the targets, up to the first 0 */
    gs_ngfx_texture depth;      /* 0: none */
    int32_t layer, mip;         /* of the targets */
    uint8_t clear_color, clear_depth;
    gs_ngfx_float4 color_value;
    float depth_value;
} gs_ngfx_pass_desc;

#pragma pack(pop)

#define GS_NGFX_API(X) \
    /* Device, window and frames. */ \
    X(uint8_t, gs_ngfx_available, (void)) \
    X(uint8_t, gs_ngfx_open, (gs_ngfx_bytes title, int64_t width, int64_t height, int64_t flags)) \
    X(uint8_t, gs_ngfx_open_headless, (int64_t width, int64_t height, int64_t flags)) \
    X(void, gs_ngfx_close, (void)) \
    X(int64_t, gs_ngfx_error, (gs_ngfx_bytes out)) \
    X(int64_t, gs_ngfx_misuse_count, (void)) \
    X(int64_t, gs_ngfx_driver, (gs_ngfx_bytes out)) \
    X(uint8_t, gs_ngfx_frame, (void)) \
    X(void, gs_ngfx_quit, (void)) \
    X(void, gs_ngfx_flush, (void)) \
    X(void, gs_ngfx_set_title, (gs_ngfx_bytes title)) \
    X(void, gs_ngfx_screen_size, (gs_ngfx_int2 *out)) \
    X(gs_ngfx_texture, gs_ngfx_screen, (void)) \
    X(gs_ngfx_texture, gs_ngfx_screen_depth, (void)) \
    X(double, gs_ngfx_time, (void)) \
    X(double, gs_ngfx_delta_time, (void)) \
    X(int64_t, gs_ngfx_frame_count, (void)) \
    X(int64_t, gs_ngfx_open_count, (void)) \
    /* Input. */ \
    X(uint8_t, gs_ngfx_key_down, (gs_ngfx_bytes name)) \
    X(uint8_t, gs_ngfx_key_pressed, (gs_ngfx_bytes name)) \
    X(uint8_t, gs_ngfx_key_released, (gs_ngfx_bytes name)) \
    X(uint8_t, gs_ngfx_mouse_down, (int64_t button)) \
    X(uint8_t, gs_ngfx_mouse_pressed, (int64_t button)) \
    X(uint8_t, gs_ngfx_mouse_released, (int64_t button)) \
    X(void, gs_ngfx_mouse_pos, (gs_ngfx_float2 *out)) \
    X(void, gs_ngfx_mouse_delta, (gs_ngfx_float2 *out)) \
    X(float, gs_ngfx_mouse_wheel, (void)) \
    X(uint8_t, gs_ngfx_mouse_in_window, (void)) \
    X(uint8_t, gs_ngfx_focused, (void)) \
    X(uint8_t, gs_ngfx_set_mouse_relative, (uint8_t on)) \
    X(uint8_t, gs_ngfx_mouse_relative, (void)) \
    X(uint8_t, gs_ngfx_set_cursor_visible, (uint8_t visible)) \
    X(uint8_t, gs_ngfx_cursor_visible, (void)) \
    X(uint8_t, gs_ngfx_inject_focus, (uint8_t focused)) \
    X(uint8_t, gs_ngfx_inject_key, (gs_ngfx_bytes name, uint8_t down)) \
    X(void, gs_ngfx_inject_mouse, (float x, float y, int64_t button, uint8_t down)) \
    X(int64_t, gs_ngfx_events, (gs_ngfx_event_slice out)) \
    X(void, gs_ngfx_text_input, (uint8_t on)) \
    X(uint8_t, gs_ngfx_inject_text, (gs_ngfx_bytes text)) \
    X(int64_t, gs_ngfx_scancode, (gs_ngfx_bytes name)) \
    X(int64_t, gs_ngfx_key_name, (int64_t scancode, gs_ngfx_bytes out)) \
    X(int64_t, gs_ngfx_clipboard, (gs_ngfx_bytes out)) \
    X(uint8_t, gs_ngfx_set_clipboard, (gs_ngfx_bytes text)) \
    /* Buffers: GPU memory with an address, and this frame's data. */ \
    X(gs_ngfx_buffer, gs_ngfx_create_buffer, (int64_t size, gs_ngfx_bytes data)) \
    X(uint8_t, gs_ngfx_update_buffer, (gs_ngfx_buffer b, int64_t offset, gs_ngfx_bytes data)) \
    X(uint8_t, gs_ngfx_read_buffer, (gs_ngfx_buffer b, int64_t offset, gs_ngfx_bytes out)) \
    X(int64_t, gs_ngfx_buffer_size, (gs_ngfx_buffer b)) \
    X(uint64_t, gs_ngfx_buffer_address, (gs_ngfx_buffer b)) \
    X(void, gs_ngfx_release_buffer, (gs_ngfx_buffer b)) \
    X(uint64_t, gs_ngfx_frame_data, (gs_ngfx_bytes data)) \
    /* Textures and samplers. */ \
    X(gs_ngfx_texture, gs_ngfx_create_texture, (const gs_ngfx_texture_desc *desc)) \
    X(gs_ngfx_texture, gs_ngfx_load_texture, (gs_ngfx_bytes path, int64_t flags)) \
    X(uint8_t, gs_ngfx_update_texture, (gs_ngfx_texture t, const gs_ngfx_region *region, gs_ngfx_bytes data)) \
    X(uint8_t, gs_ngfx_read_texture, (gs_ngfx_texture t, const gs_ngfx_region *region, gs_ngfx_bytes out)) \
    X(uint8_t, gs_ngfx_save_png, (gs_ngfx_texture t, gs_ngfx_bytes path)) \
    X(uint8_t, gs_ngfx_texture_info, (gs_ngfx_texture t, gs_ngfx_texture_desc *out)) \
    X(uint32_t, gs_ngfx_texture_index, (gs_ngfx_texture t)) \
    X(uint32_t, gs_ngfx_storage_index, (gs_ngfx_texture t)) \
    X(uint32_t, gs_ngfx_storage_mip_index, (gs_ngfx_texture t, int64_t mip)) \
    X(void, gs_ngfx_release_texture, (gs_ngfx_texture t)) \
    X(gs_ngfx_sampler, gs_ngfx_create_sampler, (const gs_ngfx_sampler_desc *desc)) \
    X(uint32_t, gs_ngfx_sampler_index, (gs_ngfx_sampler s)) \
    X(void, gs_ngfx_release_sampler, (gs_ngfx_sampler s)) \
    /* Pipelines, from embed_slang blobs (src/ngfx/ngfx_blob.h). */ \
    X(gs_ngfx_pipeline, gs_ngfx_create_pipeline, (gs_ngfx_bytes shader, gs_ngfx_bytes vertex, gs_ngfx_bytes fragment, const gs_ngfx_pipeline_desc *desc)) \
    X(void, gs_ngfx_release_pipeline, (gs_ngfx_pipeline p)) \
    X(gs_ngfx_compute_pipeline, gs_ngfx_create_compute_pipeline, (gs_ngfx_bytes shader, gs_ngfx_bytes entry)) \
    X(void, gs_ngfx_release_compute_pipeline, (gs_ngfx_compute_pipeline p)) \
    X(gs_ngfx_compute_pipeline, gs_ngfx_mip_pipeline, (void)) \
    /* Passes, drawing and compute. */ \
    X(uint8_t, gs_ngfx_begin_pass, (const gs_ngfx_pass_desc *desc)) \
    X(void, gs_ngfx_end_pass, (void)) \
    X(void, gs_ngfx_bind_pipeline, (gs_ngfx_pipeline p)) \
    X(void, gs_ngfx_bind_compute_pipeline, (gs_ngfx_compute_pipeline p)) \
    X(void, gs_ngfx_viewport, (float x, float y, float w, float h)) \
    X(void, gs_ngfx_scissor, (int64_t x, int64_t y, int64_t w, int64_t h)) \
    X(void, gs_ngfx_draw, (uint64_t root, int64_t vertices, int64_t instances, int64_t first_vertex, int64_t first_instance)) \
    X(void, gs_ngfx_draw_indexed, (uint64_t root, gs_ngfx_buffer indices, int64_t index_size, int64_t count, int64_t instances, int64_t first_index, int64_t vertex_offset, int64_t first_instance)) \
    X(void, gs_ngfx_dispatch, (uint64_t root, int64_t x, int64_t y, int64_t z)) \
    /* Synchronization the program states: what `before` did, before `after` does it. */ \
    X(void, gs_ngfx_barrier, (int32_t before, int32_t after))

#define GS_NGFX_PROTO(ret, name, params) ret name params;
GS_NGFX_API(GS_NGFX_PROTO)
#undef GS_NGFX_PROTO

/* The constants stdlib/ngfx.goose declares, with their Goose types; here they
   are GS_NGFX_<name>. The ones gfx also has keep gfx's values. */
#define GS_NGFX_CONSTANTS(X) \
    /* open() and open_headless() flags. */ \
    X(i32, WINDOW_RESIZABLE, 1) \
    X(i32, WINDOW_HIDDEN, 2) \
    X(i32, WINDOW_FULLSCREEN, 4) \
    /* Texture kinds. */ \
    X(i32, TEXTURE_2D, 0) \
    X(i32, TEXTURE_2D_ARRAY, 1) \
    X(i32, TEXTURE_3D, 2) \
    X(i32, TEXTURE_CUBE, 3) \
    /* Texture usage, combined with |. */ \
    X(i32, SAMPLED, 1) \
    X(i32, COLOR_TARGET, 2) \
    X(i32, DEPTH_TARGET, 4) \
    X(i32, STORAGE, 8) \
    /* Texture formats. DEPTH is DEPTH32F. */ \
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
    X(i32, DEPTH32F, 18) \
    X(i32, DEPTH24_STENCIL8, 19) \
    X(i32, DEPTH, 20) \
    /* Samplers. */ \
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
    /* Pipelines. */ \
    X(i32, CULL_NONE, 0) \
    X(i32, CULL_FRONT, 1) \
    X(i32, CULL_BACK, 2) \
    X(i32, BLEND_NONE, 0) \
    X(i32, BLEND_ALPHA, 1) \
    X(i32, BLEND_ADD, 2) \
    X(i32, BLEND_PREMULTIPLIED, 3) \
    X(i32, BLEND_MULTIPLY, 4) \
    /* load_texture() flags. */ \
    X(i32, LOAD_SRGB, 2) \
    /* Barrier points: a NoGraphicsAPI Stage mask in bits 0-11 and an Access mask in bits 16-27, each with
       NoGraphicsAPI's own bit values, so points combine with | as its masks do. */ \
    X(i32, STAGE_INDIRECT, 1) \
    X(i32, STAGE_INDEX_INPUT, 2) \
    X(i32, STAGE_VERTEX, 4) \
    X(i32, STAGE_TASK, 8) \
    X(i32, STAGE_MESH, 16) \
    X(i32, STAGE_DEPTH, 32) \
    X(i32, STAGE_FRAGMENT, 64) \
    X(i32, STAGE_COLOR_OUTPUT, 128) \
    X(i32, STAGE_COMPUTE, 256) \
    X(i32, STAGE_TRANSFER, 512) \
    X(i32, STAGE_ALL, 2048) \
    X(i32, ACCESS_TRANSFER_READ, 65536) \
    X(i32, ACCESS_TRANSFER_WRITE, 131072) \
    X(i32, ACCESS_SHADER_READ, 262144) \
    X(i32, ACCESS_SHADER_WRITE, 524288) \
    X(i32, ACCESS_COLOR_READ, 1048576) \
    X(i32, ACCESS_COLOR_WRITE, 2097152) \
    X(i32, ACCESS_DEPTH_READ, 4194304) \
    X(i32, ACCESS_DEPTH_WRITE, 8388608) \
    X(i32, ACCESS_INDIRECT_READ, 16777216) \
    X(i32, ACCESS_INDEX_READ, 33554432) \
    X(i32, ACCESS_DESCRIPTOR_READ, 134217728) \
    /* Named points: STAGE_ | ACCESS_ combinations. */ \
    X(i32, COMPUTE_WRITE, 524544) \
    X(i32, COMPUTE_READ, 262400) \
    X(i32, VERTEX_READ, 262148) \
    X(i32, FRAGMENT_READ, 262208) \
    X(i32, TARGET_WRITE, 10485920) \
    X(i32, INDEX_READ, 33554434) \
    /* Mouse buttons. */ \
    X(i32, MOUSE_LEFT, 1) \
    X(i32, MOUSE_MIDDLE, 2) \
    X(i32, MOUSE_RIGHT, 3) \
    X(i32, MOUSE_X1, 4) \
    X(i32, MOUSE_X2, 5) \
    /* Input events, and the modifier keys held with a key. */ \
    X(i32, EVENT_KEY, 1) \
    X(i32, EVENT_TEXT, 2) \
    X(i32, EVENT_MOUSE_MOTION, 3) \
    X(i32, EVENT_MOUSE_BUTTON, 4) \
    X(i32, EVENT_MOUSE_WHEEL, 5) \
    X(i32, EVENT_FOCUS_GAINED, 6) \
    X(i32, EVENT_FOCUS_LOST, 7) \
    X(i32, MOD_SHIFT, 1) \
    X(i32, MOD_CTRL, 2) \
    X(i32, MOD_ALT, 4) \
    X(i32, MOD_GUI, 8)

#define GS_NGFX_ENUM(type, name, value) GS_NGFX_##name = value,
enum { GS_NGFX_CONSTANTS(GS_NGFX_ENUM) };
#undef GS_NGFX_ENUM

#ifdef __cplusplus
}
#endif

#endif
