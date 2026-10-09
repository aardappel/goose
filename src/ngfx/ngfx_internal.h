/* Shared by the ngfx layer's own files in src/ngfx/, never by a program.
   ngfx_platform.c is gfx's platform half (src/gfx/gfx_device.c) with
   gs_ngfx_ names: SDL's window, input, time and errors. The GPU half is C++
   over NoGraphicsAPI (ngfx_gpu.hpp), reached through the hooks below.
   Everything is main-thread only, as windowing is. */

#ifndef GS_NGFX_INTERNAL_H
#define GS_NGFX_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ngfx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- errors (ngfx_platform.c) --------------------------------------------- */

/* Something outside the program's control failed: the reason is kept for
   gs_ngfx_error. Returns false, for `return ngfx_fail(...)`. */
bool ngfx_fail(const char *fmt, ...);
/* The program used the API wrongly: kept, counted and printed, and the
   Goose side aborts on it at the next frame. Returns false. */
bool ngfx_misuse(const char *fmt, ...);
/* True inside open() .. close(); a misuse to call `what` otherwise. */
bool ngfx_need_device(const char *what);
/* The text of the last failure, for keeping across a close(). */
const char *ngfx_error_text(void);
/* `s` as a NUL-terminated string in `buf`, cut to fit. */
const char *ngfx_cstr(gs_ngfx_bytes s, char *buf, size_t cap);

/* --- the GPU half (ngfx_device.cpp) ----------------------------------------- */

/* The device, drawing to `layer` (a CAMetalLayer on Apple, an HWND on
   Windows) or headless for NULL, with a screen of w x h pixels. */
bool ngfx_gpu_open(void *layer, int w, int h);
/* Waits for the GPU and releases everything; safe after a failed open. */
void ngfx_gpu_close(void);
/* Ends the frame being recorded: submits it, after copying the screen into
   the window's image when `present`. Then starts the next one. */
void ngfx_gpu_next_frame(bool present);
/* Replaces the screen's textures with ones of w x h pixels, keeping their
   handles. */
bool ngfx_gpu_resize(int w, int h);
/* Inside a render pass. */
bool ngfx_gpu_in_pass(void);
/* Submits what has been recorded and waits for the GPU to finish it. */
void ngfx_gpu_finish(void);
/* "metal" or "vulkan". */
const char *ngfx_gpu_driver(void);
void ngfx_gpu_screen_size(int *w, int *h);

/* For load_texture and save_png, which the platform half's SDL does. */
bool ngfx_png_load(const char *path, uint8_t **rgba, int *w, int *h);
bool ngfx_png_save(const char *path, const uint8_t *rgba, int w, int h, bool bgra);
void ngfx_png_free(uint8_t *rgba);

#ifdef __cplusplus
}
#endif

#endif
