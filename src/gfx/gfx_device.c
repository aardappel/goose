/* The gfx layer: the device, the window and its screen, frames, input and
   time, plus the handle tables and error reporting the other files share. */

#include "gfx_internal.h"

#include <stdarg.h>
#include <stdlib.h>

gfx_state gfx;

/* --- errors ---------------------------------------------------------------- */

static void gfx_vset(const char *fmt, va_list args) {
    vsnprintf(gfx.error, sizeof gfx.error, fmt, args);
}

bool gfx_fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gfx_vset(fmt, args);
    va_end(args);
    return false;
}

bool gfx_misuse(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gfx_vset(fmt, args);
    va_end(args);
    /* The Goose side aborts with the latest at the next frame; the first few
       are shown as they happen, since a misuse often causes others. */
    if (++gfx.misuse <= 8) fprintf(stderr, "gfx: %s\n", gfx.error);
    return false;
}

bool gfx_sdl_fail(const char *what) {
    return gfx_fail("%s: %s", what, SDL_GetError());
}

bool gfx_need_device(const char *what) {
    if (gfx.dev) return true;
    return gfx_misuse("%s: gfx is not open (call gfx::open or gfx::open_headless first)", what);
}

/* SDL_GPU checks its own usage with release asserts, whose default handler
   shows a dialog: a misuse here instead, and the call is skipped. */
static SDL_AssertState SDLCALL gfx_assert_handler(const SDL_AssertData *data, void *user) {
    (void)user;
    gfx_misuse("SDL: %s (%s:%d)", data->condition, data->filename, data->linenum);
    return SDL_ASSERTION_IGNORE;
}

int64_t gs_gfx_error(gs_gfx_bytes out) {
    int64_t n = (int64_t)strlen(gfx.error);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, gfx.error, (size_t)k);
    return n;
}

int64_t gs_gfx_misuse_count(void) { return gfx.misuse; }

/* --- handle tables --------------------------------------------------------- */

void *gfx_table_add(gfx_table *t, uint32_t *id) {
    uint32_t i;
    if (t->nfree) {
        i = t->freelist[--t->nfree];
    } else {
        if (t->count == t->cap) {
            uint32_t cap = t->cap ? t->cap * 2 : 64;
            if (cap > GFX_INDEX_MASK) {
                gfx_fail("too many %ss", t->what);
                return NULL;
            }
            t->items = (unsigned char *)realloc(t->items, (size_t)cap * t->item_size);
            t->gens = (uint16_t *)realloc(t->gens, cap * sizeof(uint16_t));
            t->live = (uint8_t *)realloc(t->live, cap);
            t->freelist = (uint32_t *)realloc(t->freelist, cap * sizeof(uint32_t));
            t->cap = cap;
        }
        i = t->count++;
        t->gens[i] = 0;
    }
    t->gens[i] = (uint16_t)(t->gens[i] % 4095 + 1);
    t->live[i] = 1;
    void *item = t->items + (size_t)i * t->item_size;
    memset(item, 0, t->item_size);
    *id = ((uint32_t)t->gens[i] << GFX_INDEX_BITS) | i;
    return item;
}

void *gfx_table_find(gfx_table *t, uint32_t id) {
    uint32_t i = id & GFX_INDEX_MASK;
    if (!id || i >= t->count || !t->live[i] || t->gens[i] != id >> GFX_INDEX_BITS) return NULL;
    return t->items + (size_t)i * t->item_size;
}

void *gfx_table_get(gfx_table *t, uint32_t id) {
    void *item = gfx_table_find(t, id);
    if (!item) {
        if (!id) gfx_misuse("a null %s handle", t->what);
        else gfx_misuse("a %s handle that was released, or never created", t->what);
    }
    return item;
}

void gfx_table_remove(gfx_table *t, uint32_t id) {
    if (!gfx_table_find(t, id)) return;
    uint32_t i = id & GFX_INDEX_MASK;
    t->live[i] = 0;
    t->freelist[t->nfree++] = i;
}

void gfx_table_each(gfx_table *t, void (*f)(void *item)) {
    for (uint32_t i = 0; i < t->count; i++)
        if (t->live[i]) f(t->items + (size_t)i * t->item_size);
}

void gfx_table_clear(gfx_table *t) {
    free(t->items);
    free(t->gens);
    free(t->live);
    free(t->freelist);
    const char *what = t->what;
    uint32_t size = t->item_size;
    memset(t, 0, sizeof *t);
    t->what = what;
    t->item_size = size;
}

static void gfx_init_tables(void) {
    gfx.buffers.what = "buffer";
    gfx.buffers.item_size = sizeof(gfx_buffer_slot);
    gfx.textures.what = "texture";
    gfx.textures.item_size = sizeof(gfx_texture_slot);
    gfx.samplers.what = "sampler";
    gfx.samplers.item_size = sizeof(gfx_sampler_slot);
    gfx.pipelines.what = "pipeline";
    gfx.pipelines.item_size = sizeof(gfx_pipeline_slot);
    gfx.computes.what = "compute pipeline";
    gfx.computes.item_size = sizeof(gfx_compute_slot);
}

/* --- command buffers ------------------------------------------------------- */

SDL_GPUCommandBuffer *gfx_cmd(void) {
    if (!gfx.cmd) {
        gfx.cmd = SDL_AcquireGPUCommandBuffer(gfx.dev);
        if (!gfx.cmd) gfx_sdl_fail("acquiring a command buffer");
        memset(gfx.uniforms_pushed, 0, sizeof gfx.uniforms_pushed);
    }
    return gfx.cmd;
}

bool gfx_submit(void) {
    if (!gfx.cmd) return true;
    SDL_GPUCommandBuffer *cb = gfx.cmd;
    gfx.cmd = NULL;
    return SDL_SubmitGPUCommandBuffer(cb) || gfx_sdl_fail("submitting commands");
}

bool gfx_finish(void) {
    /* An empty command buffer's fence still follows all earlier work on the
       queue, which is what waiting here is for. */
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return false;
    gfx.cmd = NULL;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) return gfx_sdl_fail("submitting commands");
    bool ok = SDL_WaitForGPUFences(gfx.dev, true, &fence, 1);
    SDL_ReleaseGPUFence(gfx.dev, fence);
    return ok || gfx_sdl_fail("waiting for the GPU");
}

void gs_gfx_flush(void) {
    if (!gfx_need_device("flush")) return;
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("flush inside a pass: end the pass first");
        return;
    }
    gfx_finish();
}

/* --- the screen -------------------------------------------------------------- */

/* A 2D texture in a slot the layer owns: the screen's, which keeps its handle
   when a resize replaces the texture. */
static bool gfx_screen_texture(gs_gfx_texture *h, SDL_GPUTextureFormat format, uint32_t usage,
                               int32_t goose_format, int w, int h_) {
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = format;
    ci.usage = usage;
    ci.width = (Uint32)w;
    ci.height = (Uint32)h_;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(gfx.dev, &ci);
    if (!tex) return gfx_sdl_fail("creating the screen");
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, h->id);
    if (s) {
        SDL_ReleaseGPUTexture(gfx.dev, s->tex);
    } else {
        s = (gfx_texture_slot *)gfx_table_add(&gfx.textures, &h->id);
        if (!s) {
            SDL_ReleaseGPUTexture(gfx.dev, tex);
            return false;
        }
    }
    s->tex = tex;
    s->format = format;
    s->usage = usage;
    s->desc.kind = GS_GFX_TEXTURE_2D;
    s->desc.format = goose_format;
    s->desc.usage = (int32_t)usage;
    s->desc.width = w;
    s->desc.height = h_;
    s->desc.depth = 1;
    s->desc.mips = 1;
    s->desc.samples = 1;
    return true;
}

bool gfx_create_screen(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_GPUTextureFormat depth;
    gfx_texture_format(GS_GFX_DEPTH, &depth);
    if (!gfx_screen_texture(&gfx.screen, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                            GS_GFX_RGBA8, w, h) ||
        !gfx_screen_texture(&gfx.screen_depth, depth, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                            GS_GFX_DEPTH, w, h))
        return false;
    gfx.width = w;
    gfx.height = h;
    /* Black and far until the program draws, rather than whatever the
       memory held. */
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return false;
    SDL_GPUColorTargetInfo ct;
    SDL_zero(ct);
    ct.texture = gfx_texture_slot_of(gfx.screen)->tex;
    ct.clear_color.a = 1.0f;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_zero(dt);
    dt.texture = gfx_texture_slot_of(gfx.screen_depth)->tex;
    dt.clear_depth = 1.0f;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    dt.stencil_store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *p = SDL_BeginGPURenderPass(cb, &ct, 1, &dt);
    if (p) SDL_EndGPURenderPass(p);
    return true;
}

gs_gfx_texture gs_gfx_screen(void) {
    gfx_need_device("screen");
    return gfx.screen;
}

gs_gfx_texture gs_gfx_screen_depth(void) {
    gfx_need_device("screen_depth");
    return gfx.screen_depth;
}

void gs_gfx_screen_size(gs_gfx_int2 *out) {
    out->x = gfx.width;
    out->y = gfx.height;
}

/* --- open and close -------------------------------------------------------- */

uint8_t gs_gfx_available(void) { return 1; }

/* A NUL-terminated copy of a Goose string, for SDL. */
static const char *gfx_cstr(gs_gfx_bytes s, char *buf, size_t cap) {
    size_t n = s.len < 0 ? 0 : (size_t)s.len;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

static bool gfx_start(bool windowed, gs_gfx_bytes title, int64_t width, int64_t height,
                      int64_t flags) {
    if (gfx.dev) return gfx_misuse("gfx is already open: close() it first");
    /* A windowed program run by a test runner draws off screen instead. */
    const char *headless = SDL_getenv("GOOSE_GFX_HEADLESS");
    if (headless && SDL_atoi(headless)) windowed = false;
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        return gfx_misuse("a screen of %lld x %lld pixels", (long long)width, (long long)height);
    gfx.error[0] = 0;
    gfx.flags = flags;
    gfx_init_tables();
    SDL_SetAssertionHandler(gfx_assert_handler, NULL);
    SDL_SetHint(SDL_HINT_APP_NAME, "Goose");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        if (windowed) return gfx_sdl_fail("initializing SDL video");
        /* With no display to open a window on, the offscreen video driver
           still gives SDL_GPU the video device it requires. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        if (!ok) return gfx_sdl_fail("initializing SDL video");
    }
    gfx.video_inited = true;
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    if (gfx_have_dxbc()) formats |= SDL_GPU_SHADERFORMAT_DXBC;
#ifdef NDEBUG
    bool debug = (flags & GS_GFX_DEBUG) != 0;
#else
    bool debug = true;
#endif
    const char *env = SDL_getenv("GOOSE_GFX_DEBUG");
    if (env) debug = SDL_atoi(env) != 0;
    gfx.dev = SDL_CreateGPUDevice(formats, debug, NULL);
    if (!gfx.dev) {
        gfx_fail("no GPU device: %s", SDL_GetError());
        gs_gfx_close();
        return false;
    }
    SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(gfx.dev);
    gfx.format = (have & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
               : (have & SDL_GPU_SHADERFORMAT_DXBC) ? SDL_GPU_SHADERFORMAT_DXBC
               : SDL_GPU_SHADERFORMAT_MSL;
    int pw = (int)width, ph = (int)height;
    if (windowed) {
        char buf[512];
        SDL_WindowFlags wf = 0;
        if (flags & GS_GFX_WINDOW_RESIZABLE) wf |= SDL_WINDOW_RESIZABLE;
        if (flags & GS_GFX_WINDOW_HIDDEN) wf |= SDL_WINDOW_HIDDEN;
        if (flags & GS_GFX_WINDOW_FULLSCREEN) wf |= SDL_WINDOW_FULLSCREEN;
        if (flags & GS_GFX_WINDOW_HIGH_DPI) wf |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        gfx.window = SDL_CreateWindow(gfx_cstr(title, buf, sizeof buf), (int)width, (int)height,
                                      wf);
        if (!gfx.window || !SDL_ClaimWindowForGPUDevice(gfx.dev, gfx.window)) {
            gfx_sdl_fail("creating the window");
            gs_gfx_close();
            return false;
        }
        if (flags & GS_GFX_NO_VSYNC) {
            SDL_GPUPresentMode modes[] = { SDL_GPU_PRESENTMODE_MAILBOX,
                                           SDL_GPU_PRESENTMODE_IMMEDIATE };
            for (int i = 0; i < 2; i++) {
                if (SDL_WindowSupportsGPUPresentMode(gfx.dev, gfx.window, modes[i])) {
                    SDL_SetGPUSwapchainParameters(gfx.dev, gfx.window,
                                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR, modes[i]);
                    break;
                }
            }
        }
        SDL_GetWindowSizeInPixels(gfx.window, &pw, &ph);
    }
    if (!gfx_create_screen(pw, ph)) {
        char why[sizeof gfx.error];
        memcpy(why, gfx.error, sizeof why);
        gs_gfx_close();
        memcpy(gfx.error, why, sizeof why);
        return false;
    }
    gfx.start_ns = gfx.last_ns = SDL_GetTicksNS();
    return true;
}

uint8_t gs_gfx_open(gs_gfx_bytes title, int64_t width, int64_t height, int64_t flags) {
    return gfx_start(true, title, width, height, flags);
}

uint8_t gs_gfx_open_headless(int64_t width, int64_t height, int64_t flags) {
    gs_gfx_bytes none = { NULL, 0 };
    return gfx_start(false, none, width, height, flags);
}

void gs_gfx_close(void) {
    if (gfx.dev) {
        if (gfx.pass) SDL_EndGPURenderPass(gfx.pass);
        if (gfx.cpass) SDL_EndGPUComputePass(gfx.cpass);
        gfx.pass = NULL;
        gfx.cpass = NULL;
        gfx_submit();
        SDL_WaitForGPUIdle(gfx.dev);
        gfx_release_pipelines();
        gfx_release_resources();
        if (gfx.window) SDL_ReleaseWindowFromGPUDevice(gfx.dev, gfx.window);
        SDL_DestroyGPUDevice(gfx.dev);
    }
    if (gfx.window) SDL_DestroyWindow(gfx.window);
    if (gfx.video_inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    /* What went wrong outlives the device, for gs_gfx_error after a failed
       open. */
    char error[sizeof gfx.error];
    memcpy(error, gfx.error, sizeof error);
    int64_t misuse = gfx.misuse;
    void *d3dcompiler = gfx.d3dcompiler, *d3dcompile = gfx.d3dcompile;
    memset(&gfx, 0, sizeof gfx);
    memcpy(gfx.error, error, sizeof error);
    gfx.misuse = misuse;
    gfx.d3dcompiler = d3dcompiler;
    gfx.d3dcompile = d3dcompile;
}

int64_t gs_gfx_driver(gs_gfx_bytes out) {
    const char *name = gfx.dev ? SDL_GetGPUDeviceDriver(gfx.dev) : "";
    if (!name) name = "";
    int64_t n = (int64_t)strlen(name);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, name, (size_t)k);
    return n;
}

void gs_gfx_set_title(gs_gfx_bytes title) {
    char buf[512];
    if (gfx.window) SDL_SetWindowTitle(gfx.window, gfx_cstr(title, buf, sizeof buf));
}

void gs_gfx_quit(void) { gfx.quit = true; }

/* --- frames ------------------------------------------------------------------ */

/* Shows the screen in the window: a blit into the swapchain texture, which
   may be missing (a minimized window), in which case nothing is shown. */
static void gfx_present(void) {
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return;
    SDL_GPUTexture *swap = NULL;
    Uint32 sw = 0, sh = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cb, gfx.window, &swap, &sw, &sh)) {
        gfx_sdl_fail("acquiring the window's image");
        return;
    }
    if (!swap) return;
    SDL_GPUBlitInfo bi;
    SDL_zero(bi);
    bi.source.texture = gfx_texture_slot_of(gfx.screen)->tex;
    bi.source.w = (Uint32)gfx.width;
    bi.source.h = (Uint32)gfx.height;
    bi.destination.texture = swap;
    bi.destination.w = sw;
    bi.destination.h = sh;
    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
    bi.filter = sw == (Uint32)gfx.width && sh == (Uint32)gfx.height ? SDL_GPU_FILTER_NEAREST
                                                                     : SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cb, &bi);
}

uint8_t gs_gfx_frame(void) {
    if (!gfx_need_device("frame")) return 0;
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("frame() inside a %s pass: end the pass first", gfx.pass ? "render" : "compute");
        return 0;
    }
    /* The first call starts the first frame; every later one ends one. */
    if (gfx.window && gfx.frames > 0) gfx_present();
    gfx_submit();
    memcpy(gfx.prev_keys, gfx.keys, sizeof gfx.keys);
    gfx.prev_buttons = gfx.buttons;
    gfx.mouse_dx = gfx.mouse_dy = gfx.wheel = 0;
    bool resized = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                gfx.quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (e.key.scancode < SDL_SCANCODE_COUNT) gfx.keys[e.key.scancode] = e.key.down;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                gfx.mouse_x = e.motion.x;
                gfx.mouse_y = e.motion.y;
                gfx.mouse_dx += e.motion.xrel;
                gfx.mouse_dy += e.motion.yrel;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button < 32) {
                    uint32_t bit = 1u << e.button.button;
                    gfx.buttons = e.button.down ? gfx.buttons | bit : gfx.buttons & ~bit;
                }
                gfx.mouse_x = e.button.x;
                gfx.mouse_y = e.button.y;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                gfx.wheel += e.wheel.y;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                resized = true;
                break;
            default:
                break;
        }
    }
    if (resized && gfx.window) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(gfx.window, &w, &h);
        if (w > 0 && h > 0 && (w != gfx.width || h != gfx.height)) gfx_create_screen(w, h);
    }
    uint64_t now = SDL_GetTicksNS();
    gfx.delta = (double)(now - gfx.last_ns) / 1e9;
    gfx.last_ns = now;
    gfx.frames++;
    return !gfx.quit;
}

double gs_gfx_time(void) {
    return gfx.dev ? (double)(SDL_GetTicksNS() - gfx.start_ns) / 1e9 : 0.0;
}

double gs_gfx_delta_time(void) { return gfx.delta; }

int64_t gs_gfx_frame_count(void) { return gfx.frames; }

/* --- input ------------------------------------------------------------------- */

static SDL_Scancode gfx_scancode(gs_gfx_bytes name) {
    char buf[64];
    SDL_Scancode sc = SDL_GetScancodeFromName(gfx_cstr(name, buf, sizeof buf));
    if (sc == SDL_SCANCODE_UNKNOWN) gfx_misuse("no key is called '%s'", buf);
    return sc;
}

uint8_t gs_gfx_key_down(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && gfx.keys[sc];
}

uint8_t gs_gfx_key_pressed(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && gfx.keys[sc] && !gfx.prev_keys[sc];
}

uint8_t gs_gfx_key_released(gs_gfx_bytes name) {
    SDL_Scancode sc = gfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && !gfx.keys[sc] && gfx.prev_keys[sc];
}

static uint32_t gfx_button(int64_t button) {
    if (button < 1 || button > 31) {
        gfx_misuse("no mouse button %lld", (long long)button);
        return 0;
    }
    return 1u << button;
}

uint8_t gs_gfx_mouse_down(int64_t button) { return (gfx.buttons & gfx_button(button)) != 0; }

uint8_t gs_gfx_mouse_pressed(int64_t button) {
    uint32_t bit = gfx_button(button);
    return (gfx.buttons & bit) && !(gfx.prev_buttons & bit);
}

uint8_t gs_gfx_mouse_released(int64_t button) {
    uint32_t bit = gfx_button(button);
    return !(gfx.buttons & bit) && (gfx.prev_buttons & bit);
}

void gs_gfx_mouse_pos(gs_gfx_float2 *out) {
    out->x = gfx.mouse_x;
    out->y = gfx.mouse_y;
}

void gs_gfx_mouse_delta(gs_gfx_float2 *out) {
    out->x = gfx.mouse_dx;
    out->y = gfx.mouse_dy;
}

float gs_gfx_mouse_wheel(void) { return gfx.wheel; }

/* Queues an input event as if it came from the keyboard or mouse, seen at
   the next frame(): what tests drive input with. */
uint8_t gs_gfx_inject_key(gs_gfx_bytes name, uint8_t down) {
    if (!gfx_need_device("inject_key")) return 0;
    SDL_Scancode sc = gfx_scancode(name);
    if (sc == SDL_SCANCODE_UNKNOWN) return 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.down = down != 0;
    return SDL_PushEvent(&e);
}

void gs_gfx_inject_mouse(float x, float y, int64_t button, uint8_t down) {
    if (!gfx_need_device("inject_mouse")) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - gfx.mouse_x;
    e.motion.yrel = y - gfx.mouse_y;
    SDL_PushEvent(&e);
    if (button) {
        if (!gfx_button(button)) return;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.timestamp = SDL_GetTicksNS();
        e.button.button = (Uint8)button;
        e.button.down = down != 0;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    }
}
