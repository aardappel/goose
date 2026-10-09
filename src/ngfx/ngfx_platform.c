/* The ngfx layer's platform half: the window, input, time and errors, which
   are gfx's (src/gfx/gfx_device.c) with gs_ngfx_ names. Where gfx's code
   drives SDL_GPU, this calls the GPU half over NoGraphicsAPI
   (ngfx_device.cpp). The input code below the frames section is gfx's as it
   is, so the two modules behave alike for every program and test. */

#include "ngfx_internal.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool open;
    SDL_Window *window;
    SDL_MetalView metal_view;
    bool video_inited;

    /* Input: held state plus every transition seen in this frame, as gfx's. */
    bool quit;
    uint8_t keys[SDL_SCANCODE_COUNT];
    uint8_t pressed_keys[SDL_SCANCODE_COUNT], released_keys[SDL_SCANCODE_COUNT];
    uint32_t buttons, pressed_buttons, released_buttons;
    float mouse_x, mouse_y, mouse_dx, mouse_dy, wheel;
    bool mouse_in;
    bool focused;
    bool mouse_relative;
    bool cursor_visible;
    uint8_t button_clicks[32];
    int window_w, window_h, window_pw, window_ph;
    gs_ngfx_event *events;
    int64_t nevents, events_cap;
    char **injected;
    int ninjected, injected_cap;
    uint32_t inject_event;
    char *clipboard;

    uint64_t start_ns, last_ns;
    double delta;
    int64_t frames;

    char error[1024];
    int64_t misuse;
} ngfx_platform_state;

static ngfx_platform_state ngfx;

/* How many times ngfx was opened, which close() leaves. */
static int64_t ngfx_opens;

/* --- errors ---------------------------------------------------------------- */

static void ngfx_vset(const char *fmt, va_list args) {
    vsnprintf(ngfx.error, sizeof ngfx.error, fmt, args);
}

bool ngfx_fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ngfx_vset(fmt, args);
    va_end(args);
    return false;
}

bool ngfx_misuse(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ngfx_vset(fmt, args);
    va_end(args);
    if (++ngfx.misuse <= 8) fprintf(stderr, "ngfx: %s\n", ngfx.error);
    return false;
}

static bool ngfx_sdl_fail(const char *what) {
    return ngfx_fail("%s: %s", what, SDL_GetError());
}

bool ngfx_need_device(const char *what) {
    if (ngfx.open) return true;
    return ngfx_misuse("%s: ngfx is not open (call ngfx::open or ngfx::open_headless first)", what);
}

const char *ngfx_error_text(void) { return ngfx.error; }

int64_t gs_ngfx_error(gs_ngfx_bytes out) {
    int64_t n = (int64_t)strlen(ngfx.error);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, ngfx.error, (size_t)k);
    return n;
}

int64_t gs_ngfx_misuse_count(void) { return ngfx.misuse; }

/* --- open and close -------------------------------------------------------- */

uint8_t gs_ngfx_available(void) { return 1; }

const char *ngfx_cstr(gs_ngfx_bytes s, char *buf, size_t cap) {
    size_t n = s.len < 0 ? 0 : (size_t)s.len;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

static void ngfx_update_input_scale(void) {
    int w = 0, h = 0, pw = 0, ph = 0;
    if (!ngfx.window || !SDL_GetWindowSize(ngfx.window, &w, &h) ||
        !SDL_GetWindowSizeInPixels(ngfx.window, &pw, &ph) || w <= 0 || h <= 0 || pw <= 0 ||
        ph <= 0)
        return;
    ngfx.window_w = w;
    ngfx.window_h = h;
    ngfx.window_pw = pw;
    ngfx.window_ph = ph;
}

static float ngfx_to_pixels(float v, int pixels, int size) {
    return (float)((double)v * pixels / size);
}

static void ngfx_seed_mouse(void) {
    ngfx.mouse_in = SDL_GetMouseFocus() == ngfx.window;
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && !SDL_strcmp(driver, "wayland")) return;
    float gx = 0, gy = 0;
    int wx = 0, wy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    if (!SDL_GetWindowPosition(ngfx.window, &wx, &wy)) return;
    ngfx.mouse_x = ngfx_to_pixels(gx - (float)wx, ngfx.window_pw, ngfx.window_w);
    ngfx.mouse_y = ngfx_to_pixels(gy - (float)wy, ngfx.window_ph, ngfx.window_h);
}

/* What NoGraphicsAPI presents to: the window's CAMetalLayer on Apple, its
   HWND on Windows. It cannot present on Linux. */
static void *ngfx_layer(void) {
#if defined(__APPLE__)
    ngfx.metal_view = SDL_Metal_CreateView(ngfx.window);
    return ngfx.metal_view ? SDL_Metal_GetLayer(ngfx.metal_view) : NULL;
#elif defined(_WIN32)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(ngfx.window),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
    return NULL;
#endif
}

static bool ngfx_start(bool windowed, gs_ngfx_bytes title, int64_t width, int64_t height,
                       int64_t flags) {
    if (ngfx.open) return ngfx_misuse("ngfx is already open: close() it first");
    /* A windowed program run by a test runner draws off screen instead. */
    const char *headless = SDL_getenv("GOOSE_GFX_HEADLESS");
    if (headless && SDL_atoi(headless)) windowed = false;
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        return ngfx_misuse("a screen of %lld x %lld pixels", (long long)width, (long long)height);
    ngfx.error[0] = 0;
    SDL_SetHint(SDL_HINT_APP_NAME, "Goose");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        if (windowed) return ngfx_sdl_fail("initializing SDL video");
        /* Input and time still go through SDL's events headless. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        if (!ok) return ngfx_sdl_fail("initializing SDL video");
    }
    ngfx.video_inited = true;
    int pw = (int)width, ph = (int)height;
    ngfx.window_w = ngfx.window_h = ngfx.window_pw = ngfx.window_ph = 1;
    void *layer = NULL;
    if (windowed) {
#if !defined(__APPLE__) && !defined(_WIN32)
        ngfx_fail("NoGraphicsAPI cannot present on this platform; use ngfx::open_headless");
        gs_ngfx_close();
        return false;
#endif
        char buf[512];
        SDL_WindowFlags wf = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
        wf |= SDL_WINDOW_METAL;
#endif
        if (flags & GS_NGFX_WINDOW_RESIZABLE) wf |= SDL_WINDOW_RESIZABLE;
        if (flags & GS_NGFX_WINDOW_HIDDEN) wf |= SDL_WINDOW_HIDDEN;
        if (flags & GS_NGFX_WINDOW_FULLSCREEN) wf |= SDL_WINDOW_FULLSCREEN;
        ngfx.window = SDL_CreateWindow(ngfx_cstr(title, buf, sizeof buf), (int)width,
                                       (int)height, wf);
        layer = ngfx.window ? ngfx_layer() : NULL;
        if (!layer) {
            ngfx_sdl_fail("creating the window");
            gs_ngfx_close();
            return false;
        }
        SDL_GetWindowSizeInPixels(ngfx.window, &pw, &ph);
        ngfx_update_input_scale();
        ngfx_seed_mouse();
    }
    ngfx.focused = !ngfx.window || (SDL_GetWindowFlags(ngfx.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    ngfx.cursor_visible = true;
    if (ngfx.window) SDL_ShowCursor();
    ngfx.open = true;
    if (!ngfx_gpu_open(layer, pw, ph)) {
        char why[sizeof ngfx.error];
        memcpy(why, ngfx.error, sizeof why);
        gs_ngfx_close();
        memcpy(ngfx.error, why, sizeof why);
        return false;
    }
    ngfx.start_ns = ngfx.last_ns = SDL_GetTicksNS();
    ngfx.inject_event = SDL_RegisterEvents(1);
    ngfx_opens++;
    return true;
}

uint8_t gs_ngfx_open(gs_ngfx_bytes title, int64_t width, int64_t height, int64_t flags) {
    return ngfx_start(true, title, width, height, flags);
}

uint8_t gs_ngfx_open_headless(int64_t width, int64_t height, int64_t flags) {
    gs_ngfx_bytes none = { NULL, 0 };
    return ngfx_start(false, none, width, height, flags);
}

void gs_ngfx_close(void) {
    ngfx_gpu_close();
    free(ngfx.events);
    for (int i = 0; i < ngfx.ninjected; i++) SDL_free(ngfx.injected[i]);
    free(ngfx.injected);
    SDL_free(ngfx.clipboard);
    if (ngfx.window) {
        SDL_SetWindowRelativeMouseMode(ngfx.window, false);
        SDL_ShowCursor();
#if defined(__APPLE__)
        if (ngfx.metal_view) SDL_Metal_DestroyView(ngfx.metal_view);
#endif
        SDL_DestroyWindow(ngfx.window);
    }
    if (ngfx.video_inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    char error[sizeof ngfx.error];
    memcpy(error, ngfx.error, sizeof error);
    int64_t misuse = ngfx.misuse;
    memset(&ngfx, 0, sizeof ngfx);
    memcpy(ngfx.error, error, sizeof error);
    ngfx.misuse = misuse;
}

int64_t gs_ngfx_driver(gs_ngfx_bytes out) {
    const char *name = ngfx.open ? ngfx_gpu_driver() : "";
    int64_t n = (int64_t)strlen(name);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, name, (size_t)k);
    return n;
}

void gs_ngfx_set_title(gs_ngfx_bytes title) {
    char buf[512];
    if (ngfx.window) SDL_SetWindowTitle(ngfx.window, ngfx_cstr(title, buf, sizeof buf));
}

void gs_ngfx_quit(void) { ngfx.quit = true; }

void gs_ngfx_flush(void) {
    if (!ngfx_need_device("flush")) return;
    if (ngfx_gpu_in_pass()) {
        ngfx_misuse("flush inside a pass: end the pass first");
        return;
    }
    ngfx_gpu_finish();
}

void gs_ngfx_screen_size(gs_ngfx_int2 *out) {
    int w = 0, h = 0;
    if (ngfx.open) ngfx_gpu_screen_size(&w, &h);
    out->x = w;
    out->y = h;
}

/* --- images ------------------------------------------------------------------ */

bool ngfx_png_load(const char *path, uint8_t **rgba, int *w, int *h) {
    size_t n = strlen(path);
    bool bmp = n >= 4 && SDL_strcasecmp(path + n - 4, ".bmp") == 0;
    SDL_Surface *loaded = bmp ? SDL_LoadBMP(path) : SDL_LoadPNG(path);
    if (!loaded) return ngfx_fail("cannot load %s: %s", path, SDL_GetError());
    SDL_Surface *conv = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!conv) return ngfx_sdl_fail("converting an image");
    size_t row = (size_t)conv->w * 4;
    uint8_t *tight = (uint8_t *)malloc(row * (size_t)conv->h);
    for (int y = 0; y < conv->h; y++)
        memcpy(tight + row * (size_t)y, (uint8_t *)conv->pixels + (size_t)conv->pitch * (size_t)y,
               row);
    *rgba = tight;
    *w = conv->w;
    *h = conv->h;
    SDL_DestroySurface(conv);
    return true;
}

bool ngfx_png_save(const char *path, const uint8_t *rgba, int w, int h, bool bgra) {
    SDL_Surface *surf = SDL_CreateSurfaceFrom(w, h, bgra ? SDL_PIXELFORMAT_BGRA32
                                                          : SDL_PIXELFORMAT_RGBA32,
                                              (void *)rgba, w * 4);
    bool ok = surf && SDL_SavePNG(surf, path);
    if (!ok) ngfx_sdl_fail("save_png");
    SDL_DestroySurface(surf);
    return ok;
}

void ngfx_png_free(uint8_t *rgba) { free(rgba); }

/* --- frames ------------------------------------------------------------------ */

/* A new event at the end of this frame's list. */
static gs_ngfx_event *ngfx_event(int32_t kind) {
    if (ngfx.nevents == ngfx.events_cap) {
        ngfx.events_cap = ngfx.events_cap ? ngfx.events_cap * 2 : 64;
        ngfx.events = (gs_ngfx_event *)realloc(ngfx.events,
                                             sizeof(gs_ngfx_event) * (size_t)ngfx.events_cap);
    }
    gs_ngfx_event *e = &ngfx.events[ngfx.nevents++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    return e;
}

/* The mouse injected input comes from, whose positions are pixels already. */
#define NGFX_INJECTED_MOUSE ((SDL_MouseID)-16)

/* The modifier keys down now, from the keys' own state, which injected key
   events keep as real ones do. */
static int32_t ngfx_mods(void) {
    int32_t m = 0;
    if (ngfx.keys[SDL_SCANCODE_LSHIFT] || ngfx.keys[SDL_SCANCODE_RSHIFT]) m |= GS_NGFX_MOD_SHIFT;
    if (ngfx.keys[SDL_SCANCODE_LCTRL] || ngfx.keys[SDL_SCANCODE_RCTRL]) m |= GS_NGFX_MOD_CTRL;
    if (ngfx.keys[SDL_SCANCODE_LALT] || ngfx.keys[SDL_SCANCODE_RALT]) m |= GS_NGFX_MOD_ALT;
    if (ngfx.keys[SDL_SCANCODE_LGUI] || ngfx.keys[SDL_SCANCODE_RGUI]) m |= GS_NGFX_MOD_GUI;
    return m;
}

/* One text event per character of UTF-8 `text`. */
static void ngfx_text_events(const char *text) {
    if (!text) return;
    Uint32 cp;
    while ((cp = SDL_StepUTF8(&text, NULL)) != 0)
        ngfx_event(GS_NGFX_EVENT_TEXT)->codepoint = cp;
}

/* Real input and the releases on focus loss take the same path, keeping
   held state, per-frame edges, and the ordered events in agreement. */
static void ngfx_key_event(SDL_Scancode sc, SDL_Keycode key, bool down, bool repeat) {
    if (sc > SDL_SCANCODE_UNKNOWN && sc < SDL_SCANCODE_COUNT) {
        if (down && !ngfx.keys[sc]) ngfx.pressed_keys[sc] = 1;
        if (!down && ngfx.keys[sc]) ngfx.released_keys[sc] = 1;
        ngfx.keys[sc] = down;
    }
    gs_ngfx_event *ev = ngfx_event(GS_NGFX_EVENT_KEY);
    ev->scancode = (int32_t)sc;
    ev->keycode = (int32_t)key;
    ev->mods = ngfx_mods();
    ev->down = down;
    ev->repeat = repeat;
}

static void ngfx_button_event(int button, bool down, int clicks) {
    if (button > 0 && button < 32) {
        uint32_t bit = 1u << button;
        if (down && !(ngfx.buttons & bit)) ngfx.pressed_buttons |= bit;
        if (!down && (ngfx.buttons & bit)) ngfx.released_buttons |= bit;
        ngfx.buttons = down ? ngfx.buttons | bit : ngfx.buttons & ~bit;
        if (down) ngfx.button_clicks[button] = (uint8_t)clicks;
    }
    gs_ngfx_event *ev = ngfx_event(GS_NGFX_EVENT_MOUSE_BUTTON);
    ev->button = button;
    ev->clicks = clicks;
    ev->down = down;
    ev->x = ngfx.mouse_x;
    ev->y = ngfx.mouse_y;
}

/* Focus is keyboard focus, not whether the mouse happens to be over us.
   Never recapture on focus gain: the game can wait for a click to resume. */
static void ngfx_focus(bool focused) {
    if (ngfx.focused == focused) return;
    ngfx.focused = focused;
    if (!focused) {
        gs_ngfx_set_mouse_relative(false);
        gs_ngfx_set_cursor_visible(true);
        for (int sc = 1; sc < SDL_SCANCODE_COUNT; sc++) {
            if (ngfx.keys[sc])
                ngfx_key_event((SDL_Scancode)sc,
                              SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false),
                              false, false);
        }
        for (int b = 1; b < 32; b++) {
            if (ngfx.buttons & (1u << b)) ngfx_button_event(b, false, ngfx.button_clicks[b]);
        }
        ngfx.mouse_dx = ngfx.mouse_dy = ngfx.wheel = 0;
    } else if (ngfx.window) {
        ngfx_seed_mouse();
    }
    ngfx_event(focused ? GS_NGFX_EVENT_FOCUS_GAINED : GS_NGFX_EVENT_FOCUS_LOST);
}

uint8_t gs_ngfx_frame(void) {
    if (!ngfx_need_device("frame")) return 0;
    if (ngfx_gpu_in_pass()) {
        ngfx_misuse("frame() inside a pass: end the pass first");
        return 0;
    }
    /* The first call starts the first frame; every later one ends one, which
       shows it in the window. */
    ngfx_gpu_next_frame(ngfx.window && ngfx.frames > 0);
    memset(ngfx.pressed_keys, 0, sizeof ngfx.pressed_keys);
    memset(ngfx.released_keys, 0, sizeof ngfx.released_keys);
    ngfx.pressed_buttons = ngfx.released_buttons = 0;
    ngfx.mouse_dx = ngfx.mouse_dy = ngfx.wheel = 0;
    ngfx.nevents = 0;
    bool resized = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                ngfx.quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (!ngfx.focused) break;
                ngfx_key_event(e.key.scancode, e.key.key, e.key.down, e.key.repeat);
            } break;
            case SDL_EVENT_TEXT_INPUT:
                if (ngfx.focused) ngfx_text_events(e.text.text);
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (!ngfx.focused) break;
                bool injected = e.motion.which == NGFX_INJECTED_MOUSE;
                int pw = injected ? 1 : ngfx.window_pw, w = injected ? 1 : ngfx.window_w;
                int ph = injected ? 1 : ngfx.window_ph, h = injected ? 1 : ngfx.window_h;
                ngfx.mouse_x = ngfx_to_pixels(e.motion.x, pw, w);
                ngfx.mouse_y = ngfx_to_pixels(e.motion.y, ph, h);
                if (injected) {
                    int sw, sh;
                    ngfx_gpu_screen_size(&sw, &sh);
                    ngfx.mouse_in = ngfx.mouse_x >= 0 && ngfx.mouse_y >= 0 &&
                                    ngfx.mouse_x < (float)sw && ngfx.mouse_y < (float)sh;
                }
                ngfx.mouse_dx += ngfx_to_pixels(e.motion.xrel, pw, w);
                ngfx.mouse_dy += ngfx_to_pixels(e.motion.yrel, ph, h);
                gs_ngfx_event *ev = ngfx_event(GS_NGFX_EVENT_MOUSE_MOTION);
                ev->x = ngfx.mouse_x;
                ev->y = ngfx.mouse_y;
            } break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (!ngfx.focused) break;
                bool injected = e.button.which == NGFX_INJECTED_MOUSE;
                ngfx.mouse_x = injected ? e.button.x
                                       : ngfx_to_pixels(e.button.x, ngfx.window_pw, ngfx.window_w);
                ngfx.mouse_y = injected ? e.button.y
                                       : ngfx_to_pixels(e.button.y, ngfx.window_ph, ngfx.window_h);
                ngfx_button_event(e.button.button, e.button.down, e.button.clicks);
            } break;
            case SDL_EVENT_MOUSE_WHEEL: {
                if (!ngfx.focused) break;
                ngfx.wheel += e.wheel.y;
                gs_ngfx_event *ev = ngfx_event(GS_NGFX_EVENT_MOUSE_WHEEL);
                ev->x = e.wheel.x;
                ev->y = e.wheel.y;
            } break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                ngfx.mouse_in = e.type == SDL_EVENT_WINDOW_MOUSE_ENTER;
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (e.window.windowID == (ngfx.window ? SDL_GetWindowID(ngfx.window) : 0))
                    ngfx_focus(e.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                ngfx_update_input_scale();
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                ngfx_update_input_scale();
                resized = true;
                break;
            default:
                if (ngfx.inject_event && e.type == ngfx.inject_event && e.user.code >= 0 &&
                    e.user.code < ngfx.ninjected) {
                    if (ngfx.focused) ngfx_text_events(ngfx.injected[e.user.code]);
                    SDL_free(ngfx.injected[e.user.code]);
                    ngfx.injected[e.user.code] = NULL;
                }
                break;
        }
    }
    /* Every injected text has been seen once the queue is empty. */
    ngfx.ninjected = 0;
    if (resized && ngfx.window) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(ngfx.window, &w, &h);
        int sw, sh;
        ngfx_gpu_screen_size(&sw, &sh);
        if (w > 0 && h > 0 && (w != sw || h != sh)) ngfx_gpu_resize(w, h);
    }
    uint64_t now = SDL_GetTicksNS();
    ngfx.delta = (double)(now - ngfx.last_ns) / 1e9;
    ngfx.last_ns = now;
    ngfx.frames++;
    return !ngfx.quit;
}

double gs_ngfx_time(void) {
    return ngfx.open ? (double)(SDL_GetTicksNS() - ngfx.start_ns) / 1e9 : 0.0;
}

double gs_ngfx_delta_time(void) { return ngfx.delta; }

int64_t gs_ngfx_frame_count(void) { return ngfx.frames; }

int64_t gs_ngfx_open_count(void) { return ngfx_opens; }

/* --- input ------------------------------------------------------------------- */

/* SDL names a few keys after their platform's labels, and knows each only
   by the name its own platform has: every one of them is taken everywhere.
   Not "Menu", which Windows, X11 and Wayland call the Application key, but
   which is also SDL's name for a key of its own. */
static const struct {
    SDL_Scancode scancode;
    const char *name;
} ngfx_key_aliases[] = {
    { SDL_SCANCODE_LALT, "Left Alt" },
    { SDL_SCANCODE_LALT, "Left Option" },
    { SDL_SCANCODE_RALT, "Right Alt" },
    { SDL_SCANCODE_RALT, "Right Option" },
    { SDL_SCANCODE_LGUI, "Left GUI" },
    { SDL_SCANCODE_LGUI, "Left Command" },
    { SDL_SCANCODE_LGUI, "Left Windows" },
    { SDL_SCANCODE_RGUI, "Right GUI" },
    { SDL_SCANCODE_RGUI, "Right Command" },
    { SDL_SCANCODE_RGUI, "Right Windows" },
    { SDL_SCANCODE_APPLICATION, "Application" },
};

static SDL_Scancode ngfx_find_scancode(const char *name) {
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    for (size_t i = 0; sc == SDL_SCANCODE_UNKNOWN && i < SDL_arraysize(ngfx_key_aliases); i++)
        if (SDL_strcasecmp(name, ngfx_key_aliases[i].name) == 0) sc = ngfx_key_aliases[i].scancode;
    return sc;
}

static SDL_Scancode ngfx_scancode(gs_ngfx_bytes name) {
    char buf[64];
    SDL_Scancode sc = ngfx_find_scancode(ngfx_cstr(name, buf, sizeof buf));
    if (sc == SDL_SCANCODE_UNKNOWN) ngfx_misuse("no key is called '%s'", buf);
    return sc;
}

uint8_t gs_ngfx_key_down(gs_ngfx_bytes name) {
    SDL_Scancode sc = ngfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && ngfx.keys[sc];
}

uint8_t gs_ngfx_key_pressed(gs_ngfx_bytes name) {
    SDL_Scancode sc = ngfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && ngfx.pressed_keys[sc];
}

uint8_t gs_ngfx_key_released(gs_ngfx_bytes name) {
    SDL_Scancode sc = ngfx_scancode(name);
    return sc != SDL_SCANCODE_UNKNOWN && ngfx.released_keys[sc];
}

static uint32_t ngfx_button(int64_t button) {
    if (button < 1 || button > 31) {
        ngfx_misuse("no mouse button %lld", (long long)button);
        return 0;
    }
    return 1u << button;
}

uint8_t gs_ngfx_mouse_down(int64_t button) { return (ngfx.buttons & ngfx_button(button)) != 0; }

uint8_t gs_ngfx_mouse_pressed(int64_t button) {
    return (ngfx.pressed_buttons & ngfx_button(button)) != 0;
}

uint8_t gs_ngfx_mouse_released(int64_t button) {
    return (ngfx.released_buttons & ngfx_button(button)) != 0;
}

void gs_ngfx_mouse_pos(gs_ngfx_float2 *out) {
    out->x = ngfx.mouse_x;
    out->y = ngfx.mouse_y;
}

void gs_ngfx_mouse_delta(gs_ngfx_float2 *out) {
    out->x = ngfx.mouse_dx;
    out->y = ngfx.mouse_dy;
}

float gs_ngfx_mouse_wheel(void) { return ngfx.wheel; }

uint8_t gs_ngfx_mouse_in_window(void) { return ngfx.mouse_in; }

uint8_t gs_ngfx_focused(void) { return ngfx.focused; }

uint8_t gs_ngfx_mouse_relative(void) { return ngfx.mouse_relative; }

uint8_t gs_ngfx_cursor_visible(void) { return ngfx.cursor_visible && !ngfx.mouse_relative; }

uint8_t gs_ngfx_set_mouse_relative(uint8_t on) {
    if (!ngfx_need_device("set_mouse_relative")) return 0;
    bool enabled = on != 0;
    if (enabled && !ngfx.focused) return ngfx_fail("set_mouse_relative: the window is not focused");
    if (ngfx.mouse_relative == enabled) return 1;
    if (ngfx.window) {
        if (!SDL_SetWindowRelativeMouseMode(ngfx.window, enabled))
            return ngfx_sdl_fail("set_mouse_relative");
        /* Hide explicitly as well, so SDL's relative-cursor-visible hint
           cannot make the query disagree with what the window displays. */
        if (enabled || !ngfx.cursor_visible) SDL_HideCursor();
        else SDL_ShowCursor();
    }
    ngfx.mouse_relative = enabled;
    /* A move before the mode change must not turn the camera afterwards.
       SDL flushes real motion too; explicitly include injected/headless motion. */
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    ngfx.mouse_dx = ngfx.mouse_dy = 0;
    return 1;
}

uint8_t gs_ngfx_set_cursor_visible(uint8_t visible) {
    if (!ngfx_need_device("set_cursor_visible")) return 0;
    bool show = visible != 0;
    if (!show && !ngfx.focused) return ngfx_fail("set_cursor_visible: the window is not focused");
    if (ngfx.window && !ngfx.mouse_relative) {
        if (!(show ? SDL_ShowCursor() : SDL_HideCursor()))
            return ngfx_sdl_fail("set_cursor_visible");
    }
    ngfx.cursor_visible = show;
    return 1;
}

uint8_t gs_ngfx_inject_focus(uint8_t focused) {
    if (!ngfx_need_device("inject_focus")) return 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    e.window.timestamp = SDL_GetTicksNS();
    e.window.windowID = ngfx.window ? SDL_GetWindowID(ngfx.window) : 0;
    return SDL_PushEvent(&e);
}

/* Queues an input event as if it came from the keyboard or mouse, seen at
   the next frame(): what tests drive input with. */
uint8_t gs_ngfx_inject_key(gs_ngfx_bytes name, uint8_t down) {
    if (!ngfx_need_device("inject_key")) return 0;
    SDL_Scancode sc = ngfx_scancode(name);
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

void gs_ngfx_inject_mouse(float x, float y, int64_t button, uint8_t down) {
    if (!ngfx_need_device("inject_mouse")) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.which = NGFX_INJECTED_MOUSE;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = x - ngfx.mouse_x;
    e.motion.yrel = y - ngfx.mouse_y;
    SDL_PushEvent(&e);
    if (button) {
        if (!ngfx_button(button)) return;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.timestamp = SDL_GetTicksNS();
        e.button.which = NGFX_INJECTED_MOUSE;
        e.button.button = (Uint8)button;
        e.button.down = down != 0;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    }
}

int64_t gs_ngfx_events(gs_ngfx_event_slice out) {
    int64_t k = ngfx.nevents < out.len ? ngfx.nevents : out.len;
    if (k > 0) memcpy(out.data, ngfx.events, sizeof(gs_ngfx_event) * (size_t)k);
    return ngfx.nevents;
}

void gs_ngfx_text_input(uint8_t on) {
    if (!ngfx_need_device("text_input") || !ngfx.window) return;
    if (on) SDL_StartTextInput(ngfx.window);
    else SDL_StopTextInput(ngfx.window);
}

/* Text as if typed, seen at the next frame(): a user event in the queue
   keeps its place among injected keys and clicks. */
uint8_t gs_ngfx_inject_text(gs_ngfx_bytes text) {
    if (!ngfx_need_device("inject_text")) return 0;
    if (!ngfx.inject_event) return ngfx_fail("inject_text: SDL has no user event left");
    if (ngfx.ninjected == ngfx.injected_cap) {
        ngfx.injected_cap = ngfx.injected_cap ? ngfx.injected_cap * 2 : 8;
        ngfx.injected = (char **)realloc(ngfx.injected, sizeof(char *) * (size_t)ngfx.injected_cap);
    }
    size_t n = text.len > 0 ? (size_t)text.len : 0;
    char *copy = (char *)SDL_malloc(n + 1);
    if (n) memcpy(copy, text.data, n);
    copy[n] = 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = ngfx.inject_event;
    e.user.timestamp = SDL_GetTicksNS();
    e.user.code = ngfx.ninjected;
    ngfx.injected[ngfx.ninjected++] = copy;
    return SDL_PushEvent(&e);
}

int64_t gs_ngfx_scancode(gs_ngfx_bytes name) {
    char buf[64];
    return ngfx_find_scancode(ngfx_cstr(name, buf, sizeof buf));
}

int64_t gs_ngfx_key_name(int64_t scancode, gs_ngfx_bytes out) {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) {
        ngfx_misuse("key_name: no scancode %lld", (long long)scancode);
        return 0;
    }
    const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);
    int64_t n = (int64_t)strlen(name);
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, name, (size_t)k);
    return n;
}

/* Headless, ngfx keeps a clipboard of its own, which keeps tests and the
   programs a test runner drives off the system's. */
int64_t gs_ngfx_clipboard(gs_ngfx_bytes out) {
    if (!ngfx_need_device("clipboard")) return 0;
    char *text = ngfx.window ? SDL_GetClipboardText() : ngfx.clipboard;
    int64_t n = text ? (int64_t)strlen(text) : 0;
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, text, (size_t)k);
    if (ngfx.window) SDL_free(text);
    return n;
}

uint8_t gs_ngfx_set_clipboard(gs_ngfx_bytes text) {
    if (!ngfx_need_device("set_clipboard")) return 0;
    size_t n = text.len > 0 ? (size_t)text.len : 0;
    char *copy = (char *)SDL_malloc(n + 1);
    if (!copy) return ngfx_fail("out of memory for the clipboard");
    if (n) memcpy(copy, text.data, n);
    copy[n] = 0;
    if (!ngfx.window) {
        SDL_free(ngfx.clipboard);
        ngfx.clipboard = copy;
        return 1;
    }
    bool ok = SDL_SetClipboardText(copy);
    SDL_free(copy);
    return ok || ngfx_sdl_fail("set_clipboard");
}
