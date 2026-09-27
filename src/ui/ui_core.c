/* The ui layer: errors, the handle tables, contexts and the checks on what
   state a call needs, input, the clipboard, what the widgets drew, and the
   utilities. */

#include "ui_internal.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>

/* --- errors --------------------------------------------------------------- */

static char ui_error_text[1024];
static int64_t ui_misuses;

static void ui_vset(const char *fmt, va_list args) {
    vsnprintf(ui_error_text, sizeof ui_error_text, fmt, args);
}

bool ui_fail(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ui_vset(fmt, args);
    va_end(args);
    return false;
}

bool ui_misuse(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ui_vset(fmt, args);
    va_end(args);
    /* The Goose side aborts with the latest at the next frame; the first few
       are shown as they happen, since one misuse often causes others. */
    if (++ui_misuses <= 8) fprintf(stderr, "ui: %s\n", ui_error_text);
    return false;
}

void ui_nk_assert(const char *expr, const char *file, int line) {
    const char *base = file;
    for (const char *p = file; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    ui_misuse("Nuklear's check %s failed (%s:%d)", expr, base, line);
}

uint8_t gs_ui_available(void) { return 1; }

int64_t gs_ui_error(gs_ui_bytes out) {
    return ui_copy_out(ui_error_text, (int64_t)strlen(ui_error_text), out);
}

int64_t gs_ui_misuse_count(void) { return ui_misuses; }

/* --- handle tables ------------------------------------------------------------ */

uint32_t ui_table_add(ui_table *t, void *item) {
    uint32_t i;
    if (t->nfree) {
        i = t->freelist[--t->nfree];
    } else {
        if (t->count == t->cap) {
            uint32_t cap = t->cap ? t->cap * 2 : 16;
            if (cap > UI_INDEX_MASK) {
                ui_fail("too many %ss", t->what);
                return 0;
            }
            t->items = (void **)realloc(t->items, cap * sizeof(void *));
            t->gens = (uint16_t *)realloc(t->gens, cap * sizeof(uint16_t));
            t->freelist = (uint32_t *)realloc(t->freelist, cap * sizeof(uint32_t));
            t->cap = cap;
        }
        i = t->count++;
        t->gens[i] = 0;
    }
    t->gens[i] = (uint16_t)(t->gens[i] % 4095 + 1);
    t->items[i] = item;
    return ((uint32_t)t->gens[i] << UI_INDEX_BITS) | i;
}

void *ui_table_find(const ui_table *t, uint32_t id) {
    uint32_t i = id & UI_INDEX_MASK;
    if (!id || i >= t->count || !t->items[i] || t->gens[i] != id >> UI_INDEX_BITS) return NULL;
    return t->items[i];
}

void *ui_table_get(const ui_table *t, uint32_t id, const char *fn) {
    void *item = ui_table_find(t, id);
    if (!item) {
        if (!id) ui_misuse("%s: a null %s handle", fn, t->what);
        else ui_misuse("%s: a %s that was destroyed, or never created", fn, t->what);
    }
    return item;
}

void ui_table_remove(ui_table *t, uint32_t id) {
    if (!ui_table_find(t, id)) return;
    uint32_t i = id & UI_INDEX_MASK;
    t->items[i] = NULL;
    t->freelist[t->nfree++] = i;
}

void *ui_table_at(const ui_table *t, uint32_t index) {
    return index < t->count ? t->items[index] : NULL;
}

ui_table ui_contexts = { "context" };

/* --- values -------------------------------------------------------------------- */

const char *ui_cstr(gs_ui_bytes s, int slot) {
    static char *bufs[4];
    static size_t caps[4];
    size_t n = s.len > 0 ? (size_t)s.len : 0;
    if (n + 1 > caps[slot]) {
        caps[slot] = n + 64;
        bufs[slot] = (char *)realloc(bufs[slot], caps[slot]);
    }
    if (n) memcpy(bufs[slot], s.data, n);
    bufs[slot][n] = 0;
    return bufs[slot];
}

int ui_int(int64_t v) {
    return v < INT_MIN ? INT_MIN : v > INT_MAX ? INT_MAX : (int)v;
}

bool ui_enum_ok(int64_t v, int64_t count, const char *fn, const char *what) {
    if (v >= 0 && v < count) return true;
    return ui_misuse("%s: %lld is not one of the %s constants", fn, (long long)v, what);
}

bool ui_symbol_ok(int64_t symbol, const char *fn) {
    return ui_enum_ok(symbol, NK_SYMBOL_MAX, fn, "SYMBOL_*");
}

bool ui_button_ok(int64_t button, const char *fn) {
    return ui_enum_ok(button, NK_BUTTON_MAX, fn, "BUTTON_* mouse button");
}

bool ui_key_ok(int64_t key, const char *fn) {
    return ui_enum_ok(key, NK_KEY_MAX, fn, "KEY_*");
}

int ui_len(gs_ui_bytes text, const char *fn) {
    if (text.len <= INT_MAX / 2) return (int)text.len;
    ui_misuse("%s: a string of %lld bytes", fn, (long long)text.len);
    return 0;
}

int64_t ui_copy_out(const void *src, int64_t n, gs_ui_bytes out) {
    int64_t k = n < out.len ? n : out.len;
    if (k > 0) memcpy(out.data, src, (size_t)k);
    return n;
}

/* --- scopes ---------------------------------------------------------------------- */

static const char *ui_scope_names[] = {
    "window", "group", "scrolled group", "list view", "popup", "combo box",
    "contextual menu", "menu", "tooltip", "menu bar", "tree node", "tree node",
    "tree element", "chart", "layout row", "row template", "layout space",
    "disabled run of widgets",
};

static const char *ui_scope_ends[] = {
    "ui::end", "ui::group_end", "ui::group_scrolled_end", "ui::list_view_end",
    "ui::popup_end", "ui::combo_end", "ui::contextual_end", "ui::menu_end",
    "ui::tooltip_end", "ui::menubar_end", "ui::tree_pop", "ui::tree_state_pop",
    "ui::tree_element_pop", "ui::chart_end", "ui::layout_row_end",
    "ui::layout_row_template_end", "ui::layout_space_end", "ui::widget_disable_end",
};

/* Whether a scope of this kind has a panel of its own that widgets go in. */
static bool ui_is_panel(uint8_t kind) {
    return kind <= UI_SCOPE_TOOLTIP;
}

ui_scope *ui_top_scope(ui_ctx *u) {
    return u->nscopes ? &u->scopes[u->nscopes - 1] : NULL;
}

bool ui_push_scope(ui_ctx *u, enum ui_scope_kind kind, bool open, const char *fn) {
    if (u->nscopes == UI_MAX_SCOPES)
        return ui_misuse("%s: %d things begun and not yet ended", fn, UI_MAX_SCOPES);
    ui_scope *s = &u->scopes[u->nscopes++];
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)kind;
    s->open = open;
    s->begun_by = fn;
    return true;
}

bool ui_expect_scope(ui_ctx *u, enum ui_scope_kind kind, const char *fn) {
    ui_scope *s = ui_top_scope(u);
    if (s && s->kind == kind) return true;
    bool open = false;
    for (int i = 0; i < u->nscopes; i++) open |= u->scopes[i].kind == kind;
    if (!open) return ui_misuse("%s with no %s begun", fn, ui_scope_names[kind]);
    return ui_misuse("%s: the %s begun by %s is still open, and %s ends it", fn,
                     ui_scope_names[s->kind], s->begun_by, ui_scope_ends[s->kind]);
}

void ui_pop_scope(ui_ctx *u) {
    if (u->nscopes) u->nscopes--;
}

bool ui_outside(ui_ctx *u, const char *fn) {
    if (u->in_input)
        return ui_misuse("%s between ui::input_begin and ui::input_end", fn);
    ui_scope *s = ui_top_scope(u);
    if (s)
        return ui_misuse("%s while the %s begun by %s is open (%s ends it)", fn,
                         ui_scope_names[s->kind], s->begun_by, ui_scope_ends[s->kind]);
    return true;
}

bool ui_in_input(ui_ctx *u, const char *fn) {
    if (u->in_input) return true;
    return ui_misuse("%s outside input: input goes between ui::input_begin and ui::input_end",
                     fn);
}

/* The innermost scope with a panel of its own, or NULL. */
static ui_scope *ui_panel_scope(ui_ctx *u) {
    for (int i = u->nscopes - 1; i >= 0; i--)
        if (ui_is_panel(u->scopes[i].kind)) return &u->scopes[i];
    return NULL;
}

bool ui_in_window(ui_ctx *u, const char *fn) {
    if (u->in_input)
        return ui_misuse("%s between ui::input_begin and ui::input_end", fn);
    if (!ui_panel_scope(u) || !u->nk.current)
        return ui_misuse("%s outside a window: it goes between ui::begin and ui::end", fn);
    return true;
}

bool ui_in_panel(ui_ctx *u, const char *fn) {
    if (!ui_in_window(u, fn)) return false;
    ui_scope *p = ui_panel_scope(u);
    if (!p->open || !u->nk.current->layout)
        return ui_misuse("%s in a window that is not open: ui::begin returned false, so only "
                         "ui::end may follow", fn);
    ui_scope *s = ui_top_scope(u);
    if (s->kind == UI_SCOPE_TEMPLATE)
        return ui_misuse("%s inside a row template: ui::layout_row_template_end ends it first",
                         fn);
    return true;
}

bool ui_popup_allowed(ui_ctx *u, const char *fn) {
    ui_scope *p = ui_panel_scope(u);
    if (p && p->kind >= UI_SCOPE_POPUP && p->kind <= UI_SCOPE_TOOLTIP)
        return ui_misuse("%s inside the %s begun by %s: popups, combo boxes, menus and "
                         "tooltips cannot open inside one another", fn,
                         ui_scope_names[p->kind], p->begun_by);
    return true;
}

float *ui_frame_floats(ui_ctx *u, int64_t n) {
    if (u->nratios == u->ratios_cap) {
        u->ratios_cap = u->ratios_cap ? u->ratios_cap * 2 : 8;
        u->ratios = (float **)realloc(u->ratios, sizeof(float *) * (size_t)u->ratios_cap);
    }
    float *f = (float *)malloc(sizeof(float) * (size_t)(n > 0 ? n : 1));
    u->ratios[u->nratios++] = f;
    return f;
}

void ui_end_frame(ui_ctx *u) {
    nk_clear(&u->nk);
    u->drawn = false;
    u->converted = false;
    for (int i = 0; i < u->nratios; i++) free(u->ratios[i]);
    u->nratios = 0;
}

/* --- contexts -------------------------------------------------------------------- */

ui_ctx *ui_ctx_get(gs_ui_context c, const char *fn) {
    return (ui_ctx *)ui_table_get(&ui_contexts, c.id, fn);
}

static void ui_clip_paste(nk_handle user, struct nk_text_edit *edit) {
    ui_ctx *u = (ui_ctx *)user.ptr;
    if (u->paste_len) ui_textedit_paste(edit, u->paste, u->paste_len);
}

static void ui_clip_copy(nk_handle user, const char *text, int len) {
    ui_ctx *u = (ui_ctx *)user.ptr;
    char *copy = (char *)malloc((size_t)(len > 0 ? len : 1));
    if (len > 0) memcpy(copy, text, (size_t)len);
    free(u->copy);
    u->copy = copy;
    u->copy_len = len > 0 ? len : 0;
    u->copied = true;
}

gs_ui_context gs_ui_create_context(gs_ui_font font) {
    gs_ui_context none = { 0 };
    ui_font *f = ui_font_get(font, "ui::create_context");
    if (!f) return none;
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, f->atlas);
    if (!a->baked) {
        ui_misuse("ui::create_context: the font's atlas is not baked yet (ui::bake it first)");
        return none;
    }
    ui_ctx *u = (ui_ctx *)calloc(1, sizeof *u);
    if (!u || !nk_init_default(&u->nk, &f->font->handle)) {
        free(u);
        ui_fail("out of memory for a context");
        return none;
    }
    u->atlas = f->atlas;
    u->scale = 1;
    nk_buffer_init_default(&u->cmds);
    nk_buffer_init_default(&u->verts);
    nk_buffer_init_default(&u->idx);
    u->nk.clip.userdata = nk_handle_ptr(u);
    u->nk.clip.paste = ui_clip_paste;
    u->nk.clip.copy = ui_clip_copy;
    u->id = ui_table_add(&ui_contexts, u);
    if (!u->id) {
        nk_free(&u->nk);
        free(u);
        return none;
    }
    gs_ui_context c = { u->id };
    return c;
}

static void ui_free_context(ui_ctx *u) {
    nk_free(&u->nk);
    nk_buffer_free(&u->cmds);
    nk_buffer_free(&u->verts);
    nk_buffer_free(&u->idx);
    for (int i = 0; i < u->nratios; i++) free(u->ratios[i]);
    free(u->ratios);
    free(u->draws);
    free(u->paste);
    free(u->copy);
    free(u->styles);
    free(u);
}

void gs_ui_destroy_context(gs_ui_context c) {
    ui_ctx *u = ui_ctx_get(c, "ui::destroy");
    if (!u) return;
    ui_table_remove(&ui_contexts, c.id);
    ui_atlas *a = u->owns_atlas ? (ui_atlas *)ui_table_find(&ui_atlases, u->atlas) : NULL;
    ui_free_context(u);
    /* Its own atlas goes with it, unless another context took up its font. */
    if (a && !ui_atlas_in_use(a)) ui_free_atlas(a);
}

gs_ui_context gs_ui_create_default_context(float font_height) {
    gs_ui_context none = { 0 };
    gs_ui_font_atlas a = gs_ui_create_font_atlas();
    if (!a.id) return none;
    gs_ui_font_config config = gs_ui_default_font_config();
    gs_ui_u32_slice ranges = { NULL, 0 };
    gs_ui_font f = gs_ui_add_default_font(a, font_height, config, ranges);
    gs_ui_context c = f.id && gs_ui_bake_font_atlas(a, 1) ? gs_ui_create_context(f) : none;
    ui_ctx *u = (ui_ctx *)ui_table_find(&ui_contexts, c.id);
    if (u) {
        u->owns_atlas = true;
        u->font_height = font_height;
    } else {
        gs_ui_destroy_font_atlas(a);
    }
    return c;
}

bool ui_scale_ok(float scale, const char *fn) {
    if (scale >= 0.25f && scale <= 16) return true;
    return ui_misuse("%s: a scale of %g (0.25 to 16)", fn, (double)scale);
}

uint8_t gs_ui_set_scale(gs_ui_context c, float scale) {
    UI_CTX(c, "ui::set_scale", 0);
    if (!ui_outside(u, "ui::set_scale") || !ui_scale_ok(scale, "ui::set_scale")) return 0;
    if (u->owns_atlas && scale != u->scale) {
        /* Its own font, baked again at the scale for sharp text. */
        if (ctx->stacks.fonts.head > 0)
            return ui_misuse("ui::set_scale with fonts pushed: its own font is baked again, so "
                             "ui::style_pop_font them first");
        ui_atlas *old = (ui_atlas *)ui_table_find(&ui_atlases, u->atlas);
        ui_font *oldf = (ui_font *)ui_table_find(&ui_fonts, old->fonts[0]);
        gs_ui_font_atlas a = gs_ui_create_font_atlas();
        if (!a.id) return 0;
        gs_ui_u32_slice ranges = { NULL, 0 };
        gs_ui_font nf = gs_ui_add_default_font(a, u->font_height, gs_ui_default_font_config(),
                                               ranges);
        if (!nf.id || !gs_ui_bake_font_atlas(a, scale)) {
            gs_ui_destroy_font_atlas(a);
            return 0;
        }
        ui_font *f = (ui_font *)ui_table_find(&ui_fonts, nf.id);
        if (ctx->style.font == &oldf->font->handle) nk_style_set_font(ctx, &f->font->handle);
        u->atlas = a.id;
        if (!ui_atlas_in_use(old)) ui_free_atlas(old);
    }
    u->scale = scale;
    return 1;
}

float gs_ui_scale(gs_ui_context c) {
    UI_CTX(c, "ui::scale", 1);
    return u->scale;
}

gs_ui_font_atlas gs_ui_context_atlas(gs_ui_context c) {
    gs_ui_font_atlas none = { 0 };
    UI_CTX(c, "ui::context_atlas", none);
    gs_ui_font_atlas a = { u->atlas };
    return a;
}

uint8_t gs_ui_context_is_valid(gs_ui_context c) {
    return ui_table_find(&ui_contexts, c.id) != NULL;
}

void gs_ui_clear(gs_ui_context c) {
    UI_CTX(c, "ui::clear", );
    if (ui_outside(u, "ui::clear")) ui_end_frame(u);
}

void gs_ui_set_delta_time(gs_ui_context c, float seconds) {
    UI_CTX(c, "ui::set_delta_time", );
    if (!isfinite(seconds) || seconds < 0) {
        ui_misuse("ui::set_delta_time: %g seconds", (double)seconds);
        return;
    }
    ctx->delta_time_seconds = seconds;
}

float gs_ui_delta_time(gs_ui_context c) {
    UI_CTX(c, "ui::delta_time", 0);
    return ctx->delta_time_seconds;
}

gs_ui_font gs_ui_context_font(gs_ui_context c) {
    gs_ui_font none = { 0 };
    UI_CTX(c, "ui::font", none);
    return ui_font_handle(ctx->style.font);
}

void gs_ui_set_clipboard(gs_ui_context c, gs_ui_bytes text) {
    UI_CTX(c, "ui::set_clipboard", );
    int len = ui_len(text, "ui::set_clipboard");
    char *paste = (char *)malloc((size_t)(len > 0 ? len : 1));
    if (len > 0) memcpy(paste, text.data, (size_t)len);
    free(u->paste);
    u->paste = paste;
    u->paste_len = len;
}

int64_t gs_ui_copied(gs_ui_context c, gs_ui_bytes out) {
    UI_CTX(c, "ui::copied", -1);
    if (!u->copied) return -1;
    /* Taken once it all fits: a call with no room asks for the length. */
    if (out.len >= u->copy_len) u->copied = false;
    return ui_copy_out(u->copy, u->copy_len, out);
}

uint32_t gs_ui_null_texture(gs_ui_context c) {
    UI_CTX(c, "ui::null_texture", 0);
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, u->atlas);
    return a ? a->texture : 0;
}

/* --- input ------------------------------------------------------------------------ */

void gs_ui_input_begin(gs_ui_context c) {
    UI_CTX(c, "ui::input_begin", );
    if (u->in_input) {
        ui_misuse("ui::input_begin twice, without ui::input_end");
        return;
    }
    if (!ui_outside(u, "ui::input_begin")) return;
    /* A new frame: the last one's commands go, unless it drew nothing. */
    if (u->drawn) ui_end_frame(u);
    nk_input_begin(ctx);
    u->in_input = true;
}

/* Input comes in pixels, of which the ui's own units are `scale`, so it
   lands between Nuklear's whole ones: kept as the floats Nuklear holds. */
void gs_ui_input_motion(gs_ui_context c, int64_t x, int64_t y) {
    UI_INPUT(c, "ui::input_motion", );
    struct nk_input *in = &ctx->input;
    nk_input_motion(ctx, ui_int(x), ui_int(y));
    in->mouse.pos.x = (float)x / u->scale;
    in->mouse.pos.y = (float)y / u->scale;
    in->mouse.delta.x = in->mouse.pos.x - in->mouse.prev.x;
    in->mouse.delta.y = in->mouse.pos.y - in->mouse.prev.y;
}

void gs_ui_input_key(gs_ui_context c, int64_t key, uint8_t down) {
    UI_INPUT(c, "ui::input_key", );
    if (ui_key_ok(key, "ui::input_key")) nk_input_key(ctx, (enum nk_keys)key, down != 0);
}

void gs_ui_input_button(gs_ui_context c, int64_t button, int64_t x, int64_t y, uint8_t down) {
    UI_INPUT(c, "ui::input_button", );
    if (!ui_button_ok(button, "ui::input_button")) return;
    struct nk_mouse_button *b = &ctx->input.mouse.buttons[button];
    bool was = b->down;
    nk_input_button(ctx, (enum nk_buttons)button, ui_int(x), ui_int(y), down != 0);
    if (b->down != was) {
        b->clicked_pos.x = (float)x / u->scale;
        b->clicked_pos.y = (float)y / u->scale;
    }
}

void gs_ui_input_scroll(gs_ui_context c, gs_ui_float2 amount) {
    UI_INPUT(c, "ui::input_scroll", );
    if (!isfinite(amount.x) || !isfinite(amount.y)) {
        ui_misuse("ui::input_scroll: a scroll of %g, %g", (double)amount.x, (double)amount.y);
        return;
    }
    nk_input_scroll(ctx, ui_nk_vec2(amount));
}

void gs_ui_input_char(gs_ui_context c, uint8_t ch) {
    UI_INPUT(c, "ui::input_char", );
    nk_input_char(ctx, (char)ch);
}

void gs_ui_input_glyph(gs_ui_context c, gs_ui_bytes utf8) {
    UI_INPUT(c, "ui::input_glyph", );
    if (utf8.len < 1 || utf8.len > NK_UTF_SIZE) {
        ui_misuse("ui::input_glyph: %lld bytes, where a glyph is 1 to %d", (long long)utf8.len,
                  NK_UTF_SIZE);
        return;
    }
    nk_glyph g;
    memset(g, 0, sizeof g);
    memcpy(g, utf8.data, (size_t)utf8.len);
    nk_input_glyph(ctx, g);
}

void gs_ui_input_unicode(gs_ui_context c, uint32_t rune) {
    UI_INPUT(c, "ui::input_unicode", );
    nk_input_unicode(ctx, rune);
}

void gs_ui_input_end(gs_ui_context c) {
    UI_INPUT(c, "ui::input_end", );
    nk_input_end(ctx);
    u->in_input = false;
}

gs_ui_mouse gs_ui_input_mouse(gs_ui_context c) {
    gs_ui_mouse m;
    memset(&m, 0, sizeof m);
    UI_CTX(c, "ui::input_mouse", m);
    const struct nk_mouse *nm = &ctx->input.mouse;
    m.pos = ui_float2_of(nm->pos);
    m.prev = ui_float2_of(nm->prev);
    m.delta = ui_float2_of(nm->delta);
    m.scroll_delta = ui_float2_of(nm->scroll_delta);
    m.grab = nm->grab;
    m.grabbed = nm->grabbed;
    m.ungrab = nm->ungrab;
    return m;
}

/* The queries about the input, answered from the context's; valid at any
   point of a frame. */
#define UI_QUERY(c, fn) \
    UI_CTX(c, fn, 0); \
    const struct nk_input *in = &ctx->input

uint8_t gs_ui_input_has_mouse_click(gs_ui_context c, int64_t button) {
    UI_QUERY(c, "ui::input_has_mouse_click");
    return ui_button_ok(button, "ui::input_has_mouse_click") &&
           nk_input_has_mouse_click(in, (enum nk_buttons)button);
}

uint8_t gs_ui_input_has_mouse_click_in_rect(gs_ui_context c, int64_t button, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_has_mouse_click_in_rect");
    return ui_button_ok(button, "ui::input_has_mouse_click_in_rect") &&
           nk_input_has_mouse_click_in_rect(in, (enum nk_buttons)button, ui_nk_rect(r));
}

uint8_t gs_ui_input_has_mouse_click_in_button_rect(gs_ui_context c, int64_t button,
                                                    gs_ui_rect r) {
    UI_QUERY(c, "ui::input_has_mouse_click_in_button_rect");
    return ui_button_ok(button, "ui::input_has_mouse_click_in_button_rect") &&
           nk_input_has_mouse_click_in_button_rect(in, (enum nk_buttons)button, ui_nk_rect(r));
}

uint8_t gs_ui_input_has_mouse_click_down_in_rect(gs_ui_context c, int64_t button, gs_ui_rect r,
                                                  uint8_t down) {
    UI_QUERY(c, "ui::input_has_mouse_click_down_in_rect");
    return ui_button_ok(button, "ui::input_has_mouse_click_down_in_rect") &&
           nk_input_has_mouse_click_down_in_rect(in, (enum nk_buttons)button, ui_nk_rect(r),
                                                 down != 0);
}

uint8_t gs_ui_input_is_mouse_click_in_rect(gs_ui_context c, int64_t button, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_is_mouse_click_in_rect");
    return ui_button_ok(button, "ui::input_is_mouse_click_in_rect") &&
           nk_input_is_mouse_click_in_rect(in, (enum nk_buttons)button, ui_nk_rect(r));
}

uint8_t gs_ui_input_is_mouse_click_down_in_rect(gs_ui_context c, int64_t button, gs_ui_rect r,
                                                 uint8_t down) {
    UI_QUERY(c, "ui::input_is_mouse_click_down_in_rect");
    return ui_button_ok(button, "ui::input_is_mouse_click_down_in_rect") &&
           nk_input_is_mouse_click_down_in_rect(in, (enum nk_buttons)button, ui_nk_rect(r),
                                                down != 0);
}

uint8_t gs_ui_input_any_mouse_click_in_rect(gs_ui_context c, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_any_mouse_click_in_rect");
    return nk_input_any_mouse_click_in_rect(in, ui_nk_rect(r)) != 0;
}

uint8_t gs_ui_input_is_mouse_prev_hovering_rect(gs_ui_context c, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_is_mouse_prev_hovering_rect");
    return nk_input_is_mouse_prev_hovering_rect(in, ui_nk_rect(r)) != 0;
}

uint8_t gs_ui_input_is_mouse_hovering_rect(gs_ui_context c, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_is_mouse_hovering_rect");
    return nk_input_is_mouse_hovering_rect(in, ui_nk_rect(r)) != 0;
}

uint8_t gs_ui_input_is_mouse_hovering_still_rect(gs_ui_context c, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_is_mouse_hovering_still_rect");
    return nk_input_is_mouse_hovering_still_rect(in, ui_nk_rect(r)) != 0;
}

static bool ui_timer_ok(const float *timer, float delay, const char *fn) {
    if (isfinite(*timer) && isfinite(delay)) return true;
    return ui_misuse("%s: a timer of %g with a delay of %g", fn, (double)*timer, (double)delay);
}

uint8_t gs_ui_input_is_mouse_hovering_delay_rect(gs_ui_context c, gs_ui_rect r, float *timer,
                                                  float delay) {
    UI_CTX(c, "ui::input_is_mouse_hovering_delay_rect", 0);
    return ui_timer_ok(timer, delay, "ui::input_is_mouse_hovering_delay_rect") &&
           nk_input_is_mouse_hovering_delay_rect(ctx, ui_nk_rect(r), timer, delay);
}

uint8_t gs_ui_input_is_mouse_hovering_still_delay_rect(gs_ui_context c, gs_ui_rect r,
                                                        float *timer, float delay) {
    UI_CTX(c, "ui::input_is_mouse_hovering_still_delay_rect", 0);
    return ui_timer_ok(timer, delay, "ui::input_is_mouse_hovering_still_delay_rect") &&
           nk_input_is_mouse_hovering_still_delay_rect(ctx, ui_nk_rect(r), timer, delay);
}

uint8_t gs_ui_input_is_mouse_hovering_still_delay_clicked_rect(gs_ui_context c, gs_ui_rect r,
                                                                float *timer, float delay,
                                                                uint8_t *clicked) {
    UI_CTX(c, "ui::input_is_mouse_hovering_still_delay_clicked_rect", 0);
    if (!ui_timer_ok(timer, delay, "ui::input_is_mouse_hovering_still_delay_clicked_rect"))
        return 0;
    nk_bool k = *clicked != 0;
    nk_bool r2 = nk_input_is_mouse_hovering_still_delay_clicked_rect(ctx, ui_nk_rect(r), timer,
                                                                     delay, &k);
    *clicked = k != 0;
    return r2 != 0;
}

uint8_t gs_ui_input_is_mouse_moved(gs_ui_context c) {
    UI_QUERY(c, "ui::input_is_mouse_moved");
    return nk_input_is_mouse_moved(in) != 0;
}

uint8_t gs_ui_input_mouse_clicked(gs_ui_context c, int64_t button, gs_ui_rect r) {
    UI_QUERY(c, "ui::input_mouse_clicked");
    return ui_button_ok(button, "ui::input_mouse_clicked") &&
           nk_input_mouse_clicked(in, (enum nk_buttons)button, ui_nk_rect(r));
}

uint8_t gs_ui_input_is_mouse_down(gs_ui_context c, int64_t button) {
    UI_QUERY(c, "ui::input_is_mouse_down");
    return ui_button_ok(button, "ui::input_is_mouse_down") &&
           nk_input_is_mouse_down(in, (enum nk_buttons)button);
}

uint8_t gs_ui_input_is_mouse_pressed(gs_ui_context c, int64_t button) {
    UI_QUERY(c, "ui::input_is_mouse_pressed");
    return ui_button_ok(button, "ui::input_is_mouse_pressed") &&
           nk_input_is_mouse_pressed(in, (enum nk_buttons)button);
}

uint8_t gs_ui_input_is_mouse_released(gs_ui_context c, int64_t button) {
    UI_QUERY(c, "ui::input_is_mouse_released");
    return ui_button_ok(button, "ui::input_is_mouse_released") &&
           nk_input_is_mouse_released(in, (enum nk_buttons)button);
}

uint8_t gs_ui_input_is_key_pressed(gs_ui_context c, int64_t key) {
    UI_QUERY(c, "ui::input_is_key_pressed");
    return ui_key_ok(key, "ui::input_is_key_pressed") &&
           nk_input_is_key_pressed(in, (enum nk_keys)key);
}

uint8_t gs_ui_input_is_key_released(gs_ui_context c, int64_t key) {
    UI_QUERY(c, "ui::input_is_key_released");
    return ui_key_ok(key, "ui::input_is_key_released") &&
           nk_input_is_key_released(in, (enum nk_keys)key);
}

uint8_t gs_ui_input_is_key_down(gs_ui_context c, int64_t key) {
    UI_QUERY(c, "ui::input_is_key_down");
    return ui_key_ok(key, "ui::input_is_key_down") && nk_input_is_key_down(in, (enum nk_keys)key);
}

/* --- what was drawn ------------------------------------------------------------------ */

/* Walks the frame's command list, flattening each command into `out` as far
   as it fits, and the points of polygons and polylines and the bytes of
   texts into `points` and `text` likewise; returns the number of commands,
   and the totals of the other two in *npoints and *ntext. */
static int64_t ui_walk_commands(struct nk_context *ctx, gs_ui_command_slice out,
                                gs_ui_int2_slice points, gs_ui_bytes text, int64_t *npoints,
                                int64_t *ntext) {
    int64_t n = 0, np = 0, nt = 0;
    const struct nk_command *cmd;
    nk_foreach(cmd, ctx) {
        gs_ui_command g;
        memset(&g, 0, sizeof g);
        g.kind = (int32_t)cmd->type;
        switch (cmd->type) {
            case NK_COMMAND_SCISSOR: {
                const struct nk_command_scissor *s = (const struct nk_command_scissor *)cmd;
                g.x = s->x, g.y = s->y, g.w = s->w, g.h = s->h;
            } break;
            case NK_COMMAND_LINE: {
                const struct nk_command_line *l = (const struct nk_command_line *)cmd;
                g.line_thickness = l->line_thickness;
                g.points[0].x = l->begin.x, g.points[0].y = l->begin.y;
                g.points[1].x = l->end.x, g.points[1].y = l->end.y;
                g.color = ui_color_of(l->color);
            } break;
            case NK_COMMAND_CURVE: {
                const struct nk_command_curve *q = (const struct nk_command_curve *)cmd;
                g.line_thickness = q->line_thickness;
                g.points[0].x = q->begin.x, g.points[0].y = q->begin.y;
                g.points[1].x = q->ctrl[0].x, g.points[1].y = q->ctrl[0].y;
                g.points[2].x = q->ctrl[1].x, g.points[2].y = q->ctrl[1].y;
                g.points[3].x = q->end.x, g.points[3].y = q->end.y;
                g.color = ui_color_of(q->color);
            } break;
            case NK_COMMAND_RECT: {
                const struct nk_command_rect *r = (const struct nk_command_rect *)cmd;
                g.x = r->x, g.y = r->y, g.w = r->w, g.h = r->h;
                g.rounding = r->rounding;
                g.line_thickness = r->line_thickness;
                g.color = ui_color_of(r->color);
            } break;
            case NK_COMMAND_RECT_FILLED: {
                const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled *)cmd;
                g.x = r->x, g.y = r->y, g.w = r->w, g.h = r->h;
                g.rounding = r->rounding;
                g.color = ui_color_of(r->color);
            } break;
            case NK_COMMAND_RECT_MULTI_COLOR: {
                const struct nk_command_rect_multi_color *r =
                    (const struct nk_command_rect_multi_color *)cmd;
                g.x = r->x, g.y = r->y, g.w = r->w, g.h = r->h;
                g.left = ui_color_of(r->left);
                g.top = ui_color_of(r->top);
                g.right = ui_color_of(r->right);
                g.bottom = ui_color_of(r->bottom);
            } break;
            case NK_COMMAND_CIRCLE: {
                const struct nk_command_circle *r = (const struct nk_command_circle *)cmd;
                g.x = r->x, g.y = r->y, g.w = r->w, g.h = r->h;
                g.line_thickness = r->line_thickness;
                g.color = ui_color_of(r->color);
            } break;
            case NK_COMMAND_CIRCLE_FILLED: {
                const struct nk_command_circle_filled *r =
                    (const struct nk_command_circle_filled *)cmd;
                g.x = r->x, g.y = r->y, g.w = r->w, g.h = r->h;
                g.color = ui_color_of(r->color);
            } break;
            case NK_COMMAND_ARC: {
                const struct nk_command_arc *a = (const struct nk_command_arc *)cmd;
                g.x = a->cx, g.y = a->cy, g.w = a->r;
                g.line_thickness = a->line_thickness;
                g.a_min = a->a[0], g.a_max = a->a[1];
                g.color = ui_color_of(a->color);
            } break;
            case NK_COMMAND_ARC_FILLED: {
                const struct nk_command_arc_filled *a = (const struct nk_command_arc_filled *)cmd;
                g.x = a->cx, g.y = a->cy, g.w = a->r;
                g.a_min = a->a[0], g.a_max = a->a[1];
                g.color = ui_color_of(a->color);
            } break;
            case NK_COMMAND_TRIANGLE: {
                const struct nk_command_triangle *t = (const struct nk_command_triangle *)cmd;
                g.line_thickness = t->line_thickness;
                g.points[0].x = t->a.x, g.points[0].y = t->a.y;
                g.points[1].x = t->b.x, g.points[1].y = t->b.y;
                g.points[2].x = t->c.x, g.points[2].y = t->c.y;
                g.color = ui_color_of(t->color);
            } break;
            case NK_COMMAND_TRIANGLE_FILLED: {
                const struct nk_command_triangle_filled *t =
                    (const struct nk_command_triangle_filled *)cmd;
                g.points[0].x = t->a.x, g.points[0].y = t->a.y;
                g.points[1].x = t->b.x, g.points[1].y = t->b.y;
                g.points[2].x = t->c.x, g.points[2].y = t->c.y;
                g.color = ui_color_of(t->color);
            } break;
            case NK_COMMAND_POLYGON:
            case NK_COMMAND_POLYGON_FILLED:
            case NK_COMMAND_POLYLINE: {
                const struct nk_vec2i *pts;
                int count;
                if (cmd->type == NK_COMMAND_POLYGON_FILLED) {
                    const struct nk_command_polygon_filled *p =
                        (const struct nk_command_polygon_filled *)cmd;
                    pts = p->points, count = p->point_count;
                    g.color = ui_color_of(p->color);
                } else if (cmd->type == NK_COMMAND_POLYGON) {
                    const struct nk_command_polygon *p = (const struct nk_command_polygon *)cmd;
                    pts = p->points, count = p->point_count;
                    g.line_thickness = p->line_thickness;
                    g.color = ui_color_of(p->color);
                } else {
                    const struct nk_command_polyline *p = (const struct nk_command_polyline *)cmd;
                    pts = p->points, count = p->point_count;
                    g.line_thickness = p->line_thickness;
                    g.color = ui_color_of(p->color);
                }
                g.first = (int32_t)np;
                g.count = count;
                for (int i = 0; i < count; i++, np++)
                    if (np < points.len) {
                        points.data[np].x = pts[i].x;
                        points.data[np].y = pts[i].y;
                    }
            } break;
            case NK_COMMAND_TEXT: {
                const struct nk_command_text *t = (const struct nk_command_text *)cmd;
                g.x = t->x, g.y = t->y, g.w = t->w, g.h = t->h;
                g.color = ui_color_of(t->foreground);
                g.background = ui_color_of(t->background);
                g.font = ui_font_handle(t->font);
                g.height = t->height;
                g.first = (int32_t)nt;
                g.count = t->length;
                for (int i = 0; i < t->length; i++, nt++)
                    if (nt < text.len) text.data[nt] = (uint8_t)t->string[i];
            } break;
            case NK_COMMAND_IMAGE: {
                const struct nk_command_image *m = (const struct nk_command_image *)cmd;
                g.x = m->x, g.y = m->y, g.w = m->w, g.h = m->h;
                g.image = ui_image_of(m->img);
                g.color = ui_color_of(m->col);
            } break;
            default:
                break;
        }
        if (n < out.len) out.data[n] = g;
        n++;
    }
    *npoints = np;
    *ntext = nt;
    return n;
}

int64_t gs_ui_commands(gs_ui_context c, gs_ui_command_slice out) {
    UI_CTX(c, "ui::commands", 0);
    if (!ui_outside(u, "ui::commands")) return 0;
    gs_ui_int2_slice none = { NULL, 0 };
    gs_ui_bytes notext = { NULL, 0 };
    int64_t np, nt;
    return ui_walk_commands(ctx, out, none, notext, &np, &nt);
}

int64_t gs_ui_command_points(gs_ui_context c, gs_ui_int2_slice out) {
    UI_CTX(c, "ui::command_points", 0);
    if (!ui_outside(u, "ui::command_points")) return 0;
    gs_ui_command_slice none = { NULL, 0 };
    gs_ui_bytes notext = { NULL, 0 };
    int64_t np, nt;
    ui_walk_commands(ctx, none, out, notext, &np, &nt);
    return np;
}

int64_t gs_ui_command_text(gs_ui_context c, gs_ui_bytes out) {
    UI_CTX(c, "ui::command_text", 0);
    if (!ui_outside(u, "ui::command_text")) return 0;
    gs_ui_command_slice none = { NULL, 0 };
    gs_ui_int2_slice nopoints = { NULL, 0 };
    int64_t np, nt;
    ui_walk_commands(ctx, none, nopoints, out, &np, &nt);
    return nt;
}

int64_t gs_ui_convert(gs_ui_context c, const gs_ui_convert_config *config) {
    UI_CTX(c, "ui::convert", NK_CONVERT_INVALID_PARAM);
    if (!ui_outside(u, "ui::convert")) return NK_CONVERT_INVALID_PARAM;
    const gs_ui_convert_config *g = config;
    if (!(g->global_alpha >= 0 && g->global_alpha <= 1) || g->circle_segment_count < 3 ||
        g->arc_segment_count < 3 || g->curve_segment_count < 3 ||
        g->circle_segment_count > 1024 || g->arc_segment_count > 1024 ||
        g->curve_segment_count > 1024) {
        ui_misuse("ui::convert: a global alpha of %g (0 to 1) and segment counts of %d, %d, %d "
                  "(3 to 1024)", (double)g->global_alpha, g->circle_segment_count,
                  g->arc_segment_count, g->curve_segment_count);
        return NK_CONVERT_INVALID_PARAM;
    }
    static const struct nk_draw_vertex_layout_element layout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(gs_ui_vertex, pos) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(gs_ui_vertex, uv) },
        { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(gs_ui_vertex, color) },
        { NK_VERTEX_LAYOUT_END },
    };
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, u->atlas);
    struct nk_convert_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.vertex_layout = layout;
    cfg.vertex_size = sizeof(gs_ui_vertex);
    cfg.vertex_alignment = 4;
    cfg.tex_null.texture = nk_handle_id((int)a->texture);
    cfg.tex_null.uv = a->null_uv;
    cfg.global_alpha = g->global_alpha;
    cfg.line_AA = g->line_aa ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF;
    cfg.shape_AA = g->shape_aa ? NK_ANTI_ALIASING_ON : NK_ANTI_ALIASING_OFF;
    cfg.circle_segment_count = (unsigned)g->circle_segment_count;
    cfg.arc_segment_count = (unsigned)g->arc_segment_count;
    cfg.curve_segment_count = (unsigned)g->curve_segment_count;
    nk_buffer_clear(&u->cmds);
    nk_buffer_clear(&u->verts);
    nk_buffer_clear(&u->idx);
    nk_flags result = nk_convert(ctx, &u->cmds, &u->verts, &u->idx, &cfg);
    /* In pixels, scale times the ui's own units. */
    if (u->scale != 1) {
        gs_ui_vertex *v = (gs_ui_vertex *)nk_buffer_memory(&u->verts);
        for (nk_size i = 0; i < ctx->draw_list.vertex_count; i++) {
            v[i].pos.x *= u->scale;
            v[i].pos.y *= u->scale;
        }
    }
    /* The draws as they are now: Nuklear's iterator reads the context's
       draw list, which later calls change. Empty ones are left out. */
    u->ndraws = 0;
    const struct nk_draw_command *cmd;
    nk_draw_foreach(cmd, ctx, &u->cmds) {
        if (!cmd->elem_count) continue;
        if (u->ndraws == u->draws_cap) {
            u->draws_cap = u->draws_cap ? u->draws_cap * 2 : 64;
            u->draws = (gs_ui_draw_command *)realloc(
                u->draws, sizeof(gs_ui_draw_command) * (size_t)u->draws_cap);
        }
        gs_ui_draw_command *d = &u->draws[u->ndraws++];
        d->elem_count = cmd->elem_count;
        d->clip_rect = ui_rect_of(cmd->clip_rect);
        d->clip_rect.x *= u->scale;
        d->clip_rect.y *= u->scale;
        d->clip_rect.w *= u->scale;
        d->clip_rect.h *= u->scale;
        d->texture = (uint32_t)cmd->texture.id;
    }
    u->nverts = (int64_t)ctx->draw_list.vertex_count;
    u->nidx = (int64_t)ctx->draw_list.element_count;
    u->converted = true;
    return (int64_t)result;
}

int64_t gs_ui_vertices(gs_ui_context c, gs_ui_vertex_slice out) {
    UI_CTX(c, "ui::vertices", 0);
    if (!u->converted) return 0;
    int64_t k = u->nverts < out.len ? u->nverts : out.len;
    if (k > 0)
        memcpy(out.data, nk_buffer_memory_const(&u->verts), sizeof(gs_ui_vertex) * (size_t)k);
    return u->nverts;
}

int64_t gs_ui_indices(gs_ui_context c, gs_ui_u32_slice out) {
    UI_CTX(c, "ui::indices", 0);
    if (!u->converted) return 0;
    int64_t k = u->nidx < out.len ? u->nidx : out.len;
    if (k > 0) memcpy(out.data, nk_buffer_memory_const(&u->idx), sizeof(uint32_t) * (size_t)k);
    return u->nidx;
}

int64_t gs_ui_draw_commands(gs_ui_context c, gs_ui_draw_command_slice out) {
    UI_CTX(c, "ui::draw_commands", 0);
    if (!u->converted) return 0;
    int64_t k = u->ndraws < out.len ? u->ndraws : out.len;
    if (k > 0) memcpy(out.data, u->draws, sizeof(gs_ui_draw_command) * (size_t)k);
    return u->ndraws;
}

/* --- utilities ----------------------------------------------------------------------- */

uint32_t gs_ui_murmur_hash(gs_ui_bytes key, uint32_t seed) {
    return nk_murmur_hash(key.data, ui_len(key, "ui::murmur_hash"), seed);
}

void gs_ui_triangle_from_direction(gs_ui_rect r, float pad_x, float pad_y, int64_t direction,
                                   gs_ui_float2_slice out) {
    if (out.len != 3) {
        ui_misuse("ui::triangle_from_direction: %lld points out, where a triangle has 3",
                  (long long)out.len);
        return;
    }
    if (!ui_enum_ok(direction, 4, "ui::triangle_from_direction", "UP, RIGHT, DOWN, LEFT"))
        return;
    struct nk_vec2 result[3];
    nk_triangle_from_direction(result, ui_nk_rect(r), pad_x, pad_y, (enum nk_heading)direction);
    for (int i = 0; i < 3; i++) out.data[i] = ui_float2_of(result[i]);
}

uint8_t gs_ui_strfilter(gs_ui_bytes text, gs_ui_bytes regexp) {
    return nk_strfilter(ui_cstr(text, 0), ui_cstr(regexp, 1)) != 0;
}

static char ui_ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }

/* Nuklear's nk_to_lower subtracts where it should add, so its fuzzy match
   pairs a capital with the same capital only: both sides go in lower case,
   which forgoes its bonus for a match at a capital. */
uint8_t gs_ui_strmatch_fuzzy_text(gs_ui_bytes text, gs_ui_bytes pattern, int64_t *score) {
    int len = ui_len(text, "ui::strmatch_fuzzy_text");
    const char *p = ui_cstr(pattern, 0);
    size_t plen = strlen(p);
    char *lower = (char *)malloc((size_t)len + plen + 1);
    *score = 0;
    if (!lower) return ui_fail("out of memory for ui::strmatch_fuzzy_text");
    for (int i = 0; i < len; i++) lower[i] = ui_ascii_lower((char)text.data[i]);
    for (size_t i = 0; i <= plen; i++) lower[len + i] = ui_ascii_lower(p[i]);
    int s = 0;
    int r = nk_strmatch_fuzzy_text(lower, len, lower + len, &s);
    free(lower);
    *score = s;
    return r != 0;
}
