/* Shared by the ui layer's own C files in src/ui/, never by a program: the
   Nuklear configuration every one of them includes it with, the handle
   tables, errors, and the state the layer keeps beside each Nuklear
   object. Like the gfx layer it is used from the main thread only (the
   compiler rejects a thread_fn that reaches it). */

#ifndef GS_UI_INTERNAL_H
#define GS_UI_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every file including nuklear.h must define the same options. 32-bit
   indices, so a frame's triangles are not limited to 65536 vertices. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_UINT_DRAW_INDEX
/* Bytes of text one frame's input holds: an input method can deliver more
   than Nuklear's 16 at once. */
#define NK_INPUT_MAX 256
/* The C library's string to number conversion, which handles every input
   where Nuklear's own does not. Its number printing and trigonometry are
   kept: plain arithmetic, the same on every platform. */
#define NK_STRTOD strtod

/* Nuklear's asserts are misuses of the layer, reported and skipped, rather
   than a crash or a debugger break: the layer checks what a program can get
   wrong before Nuklear sees it, so one firing means a check is missing. */
void ui_nk_assert(const char *expr, const char *file, int line);
#define NK_ASSERT(e) ((e) ? (void)0 : ui_nk_assert(#e, __FILE__, __LINE__))

/* Nuklear's header mixes enum types in its own constants. */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 5287)
#endif
#include "nuklear.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "ui_api.h"

/* --- errors --------------------------------------------------------------- */

/* Something outside the program's control failed: the reason is kept for
   gs_ui_error. Returns false, for `return ui_fail(...)`. */
bool ui_fail(const char *fmt, ...);
/* The program used the API wrongly: kept, counted and printed, and the
   Goose side aborts on it at the next frame. Returns false. */
bool ui_misuse(const char *fmt, ...);

/* --- handle tables -----------------------------------------------------------
   A handle is (generation << 20) | index into a table of pointers: each
   object is allocated on its own, since Nuklear keeps pointers into the
   ones it owns (a context into itself, fonts into their atlas). */

#define UI_INDEX_BITS 20
#define UI_INDEX_MASK ((1u << UI_INDEX_BITS) - 1)

typedef struct {
    const char *what;           /* for messages: "context", "font atlas", ... */
    void **items;
    uint16_t *gens;
    uint32_t *freelist;
    uint32_t count, cap, nfree;
} ui_table;

uint32_t ui_table_add(ui_table *t, void *item);         /* 0 when full */
void *ui_table_find(const ui_table *t, uint32_t id);    /* NULL for 0 or a stale id */
/* As find, a misuse naming `fn` if NULL. */
void *ui_table_get(const ui_table *t, uint32_t id, const char *fn);
void ui_table_remove(ui_table *t, uint32_t id);
/* The live item at `index`, or NULL, for walking every item. */
void *ui_table_at(const ui_table *t, uint32_t index);

/* --- fonts ------------------------------------------------------------------- */

typedef struct {
    struct nk_font_atlas atlas;
    uint32_t id;
    bool baked;
    /* After baking: RGBA, width x height, kept for as long as the atlas so
       a renderer can upload it again (after gfx was closed and opened). */
    unsigned char *pixels;
    int width, height;
    uint32_t texture;           /* what a renderer said holds the pixels; 0: none yet */
    struct nk_vec2 null_uv;     /* a white texel, for drawing shapes */
    float scale;                /* its glyphs, baked this many times their fonts' size */
    uint32_t *fonts;            /* the handles of its fonts, in the order added */
    int nfonts;
    /* Custom glyph ranges, which Nuklear reads until the atlas is gone. */
    nk_rune **ranges;
    int nranges;
} ui_atlas;

typedef struct {
    struct nk_font *font;
    uint32_t atlas;             /* the atlas's handle */
} ui_font;

extern ui_table ui_atlases, ui_fonts;

ui_font *ui_font_get(gs_ui_font f, const char *fn);
/* Whether a context still uses one of the atlas's fonts; and the atlas's
   end, with its fonts. */
bool ui_atlas_in_use(ui_atlas *a);
void ui_free_atlas(ui_atlas *a);
/* The handle of the font Nuklear knows by `font`, or 0. */
gs_ui_font ui_font_handle(const struct nk_user_font *font);
/* Adds Nuklear's built-in font, one from TrueType bytes or one from a file
   (`data` its path) to an atlas, what goes wrong named after `fn`. */
enum { UI_FONT_DEFAULT, UI_FONT_MEMORY, UI_FONT_FILE };
gs_ui_font ui_add_font(gs_ui_font_atlas h, int source, gs_ui_bytes data, float height,
                       gs_ui_font_config config, gs_ui_u32_slice ranges, const char *fn);
/* A TrueType or OpenType file's bytes, malloc'd, their count in *len; or
   NULL, with the reason set. */
uint8_t *ui_read_font_file(const char *path, int64_t *len);
/* A ui scale or a font's baking one, a misuse naming `fn` if out of range. */
bool ui_scale_ok(float scale, const char *fn);

/* --- contexts ---------------------------------------------------------------- */

/* What a program has begun and not yet ended, innermost last, so each end
   can be checked against its begin, and a widget against where it is. */
enum ui_scope_kind {
    UI_SCOPE_WINDOW,
    UI_SCOPE_GROUP,
    UI_SCOPE_GROUP_SCROLLED,
    UI_SCOPE_LIST_VIEW,
    UI_SCOPE_POPUP,
    UI_SCOPE_COMBO,
    UI_SCOPE_CONTEXTUAL,
    UI_SCOPE_MENU,
    UI_SCOPE_TOOLTIP,
    UI_SCOPE_MENUBAR,
    UI_SCOPE_TREE,
    UI_SCOPE_TREE_STATE,
    UI_SCOPE_TREE_ELEMENT,
    UI_SCOPE_CHART,
    UI_SCOPE_ROW,
    UI_SCOPE_TEMPLATE,
    UI_SCOPE_SPACE,
    UI_SCOPE_DISABLED,
};

typedef struct {
    uint8_t kind;
    /* Widgets may be placed in it: a window whose begin returned false is
       still ended, but holds nothing. */
    bool open;
    const char *begun_by;       /* the Goose function that began it */
    /* A scrolled group's offsets, which Nuklear writes back when it ends. */
    nk_uint scroll_x, scroll_y;
    struct nk_list_view view;
} ui_scope;

#define UI_MAX_SCOPES 64

typedef struct {
    struct nk_context nk;
    uint32_t id;
    uint32_t atlas;             /* of the font it was made with: its white texel draws shapes */
    bool owns_atlas;            /* made for it, and destroyed with it */
    float font_height;          /* of its own atlas's font */
    /* That font's TrueType data, kept to bake it again; NULL for
       Nuklear's built-in font. */
    uint8_t *ttf;
    int64_t ttf_len;
    /* Pixels per unit of the ui's own layout: what convert() multiplies
       positions by, and input divides them by. */
    float scale;
    ui_scope scopes[UI_MAX_SCOPES];
    int nscopes;
    bool in_input;              /* between input_begin and input_end */
    bool drawn;                 /* windows begun since the last clear */
    /* The last convert's output, valid until the next convert or clear. */
    bool converted;
    struct nk_buffer cmds, verts, idx;
    gs_ui_draw_command *draws;
    int64_t ndraws, draws_cap;
    int64_t nverts, nidx;
    /* Layout row ratios, which Nuklear reads until the row ends: kept until
       the frame is cleared. */
    float **ratios;
    int nratios, ratios_cap;
    /* The clipboard: what a paste inserts, and what Nuklear last copied. */
    char *paste;
    int paste_len;
    char *copy;
    int copy_len;
    bool copied;
    /* Cursors Nuklear points at from its style. */
    struct nk_cursor cursors[NK_CURSOR_COUNT];
    /* Styles push_style saved. */
    gs_ui_style *styles;
    int nstyles, styles_cap;
} ui_ctx;

extern ui_table ui_contexts;

/* The context of a handle, or NULL and a misuse naming `fn`. */
ui_ctx *ui_ctx_get(gs_ui_context c, const char *fn);

/* The checks most entry points start with. Each is false, and a misuse
   naming `fn`, when the context is not in the state the call needs:
   - ui_outside: not inside input, and no window begun;
   - ui_in_window: a window begun, open or not (the queries about it);
   - ui_in_panel: in an open window, group or popup, where widgets go;
   - ui_in_input: between input_begin and input_end. */
bool ui_outside(ui_ctx *u, const char *fn);
bool ui_in_window(ui_ctx *u, const char *fn);
bool ui_in_panel(ui_ctx *u, const char *fn);
bool ui_in_input(ui_ctx *u, const char *fn);

/* The first lines of most entry points: `u` the context and `ctx` its
   nk_context, or return `fail` (empty for a void function). */
#define UI_CTX(c, fn, fail) \
    ui_ctx *u = ui_ctx_get(c, fn); \
    if (!u) return fail; \
    struct nk_context *ctx = &u->nk; \
    (void)ctx
#define UI_WINDOW(c, fn, fail) \
    UI_CTX(c, fn, fail); \
    if (!ui_in_window(u, fn)) return fail
#define UI_PANEL(c, fn, fail) \
    UI_CTX(c, fn, fail); \
    if (!ui_in_panel(u, fn)) return fail
#define UI_INPUT(c, fn, fail) \
    UI_CTX(c, fn, fail); \
    if (!ui_in_input(u, fn)) return fail

/* Scopes: push one begun by `fn`, or check the innermost is of `kind` and
   pop it for its end (a misuse naming `fn`, with what is open instead, if
   not). */
bool ui_push_scope(ui_ctx *u, enum ui_scope_kind kind, bool open, const char *fn);
ui_scope *ui_top_scope(ui_ctx *u);
bool ui_expect_scope(ui_ctx *u, enum ui_scope_kind kind, const char *fn);
void ui_pop_scope(ui_ctx *u);
/* Popups, combos, menus, contextual menus and tooltips cannot open inside
   one another: a misuse naming `fn` if the current panel is one. */
bool ui_popup_allowed(ui_ctx *u, const char *fn);
/* Memory for a layout row's ratios, kept until the frame is cleared. */
float *ui_frame_floats(ui_ctx *u, int64_t n);
/* The frame's end: Nuklear's clear, and the layer's per-frame state. */
void ui_end_frame(ui_ctx *u);
/* Whether a window is one of the frame's: begun since the last clear, or,
   while nothing has been begun since, any, since the clear left only the
   last frame's. A window the program stopped beginning stays in Nuklear's
   list until the next clear frees it. */
static inline bool ui_window_current(const ui_ctx *u, const struct nk_window *w) {
    return !u->drawn || w->seq == u->nk.seq;
}

/* --- values ---------------------------------------------------------------- */

/* A NUL-terminated copy of a Goose string for Nuklear, in one of a few
   buffers (`slot`) that stay valid until that slot is used again. */
const char *ui_cstr(gs_ui_bytes s, int slot);
/* Nuklear's `int` from a Goose integer, clamped. */
int ui_int(int64_t v);
/* Checks an enum argument is one of the constants for it, a misuse naming
   `fn` and `what` if not. */
bool ui_enum_ok(int64_t v, int64_t count, const char *fn, const char *what);
bool ui_symbol_ok(int64_t symbol, const char *fn);
bool ui_button_ok(int64_t button, const char *fn);
bool ui_key_ok(int64_t key, const char *fn);
/* The length of `text` Nuklear is given, with an out-of-range one a
   misuse. */
int ui_len(gs_ui_bytes text, const char *fn);
/* Copies `n` bytes of `src` into `out` as far as they fit; returns n. */
int64_t ui_copy_out(const void *src, int64_t n, gs_ui_bytes out);

static inline struct nk_rect ui_nk_rect(gs_ui_rect r) {
    return nk_rect(r.x, r.y, r.w, r.h);
}
static inline gs_ui_rect ui_rect_of(struct nk_rect r) {
    gs_ui_rect g = { r.x, r.y, r.w, r.h };
    return g;
}
static inline struct nk_vec2 ui_nk_vec2(gs_ui_float2 v) {
    return nk_vec2(v.x, v.y);
}
static inline gs_ui_float2 ui_float2_of(struct nk_vec2 v) {
    gs_ui_float2 g = { v.x, v.y };
    return g;
}
static inline struct nk_color ui_nk_color(gs_ui_color c) {
    struct nk_color n = { c.r, c.g, c.b, c.a };
    return n;
}
static inline gs_ui_color ui_color_of(struct nk_color c) {
    gs_ui_color g = { c.r, c.g, c.b, c.a };
    return g;
}
static inline struct nk_colorf ui_nk_colorf(gs_ui_colorf c) {
    struct nk_colorf n = { c.r, c.g, c.b, c.a };
    return n;
}
static inline gs_ui_colorf ui_colorf_of(struct nk_colorf c) {
    gs_ui_colorf g = { c.r, c.g, c.b, c.a };
    return g;
}
static inline struct nk_image ui_nk_image(gs_ui_image i) {
    struct nk_image n;
    memset(&n, 0, sizeof n);
    n.handle.id = (int)i.texture;
    n.w = i.w;
    n.h = i.h;
    memcpy(n.region, i.region, sizeof n.region);
    return n;
}
static inline gs_ui_image ui_image_of(struct nk_image n) {
    gs_ui_image g;
    g.texture = (uint32_t)n.handle.id;
    g.w = n.w;
    g.h = n.h;
    memcpy(g.region, n.region, sizeof g.region);
    return g;
}
static inline gs_ui_scroll ui_scroll_of(nk_uint x, nk_uint y) {
    gs_ui_scroll s = { x, y };
    return s;
}

/* The style conversions, both ways; nk_style's font and cursors are left
   as they are. From Goose the enums and item kinds are checked first, a
   misuse naming `fn`. */
void ui_style_to_gs(const struct nk_style *n, gs_ui_style *g);
bool ui_style_check(const gs_ui_style *g, const char *fn);
void ui_style_to_nk(const gs_ui_style *g, struct nk_style *n);
void ui_style_button_to_nk(const gs_ui_style_button *g, struct nk_style_button *n);
bool ui_style_button_check(const gs_ui_style_button *g, const char *fn);
void ui_style_link_to_nk(const gs_ui_style_link *g, struct nk_style_link *n);
bool ui_style_link_check(const gs_ui_style_link *g, const char *fn);

/* The nk_filter_* a FILTER_* constant stands for, NULL if none. */
nk_plugin_filter ui_filter(int64_t filter);
/* Pastes into an edit what typing the text would enter (the edit's filter,
   no line breaks on a single line), as far as it fits, as one undo step;
   false if nothing went in. Nuklear's nk_textedit_paste counts the text's
   bytes as its characters, reading past a text with longer ones. */
bool ui_textedit_paste(struct nk_text_edit *e, const char *text, int len);

typedef struct {
    struct nk_text_edit edit;
    uint32_t id;
} ui_text_edit;

extern ui_table ui_text_edits;

#endif
