/* Windows, layout, groups, trees, list views, and what opens over a window:
   popups, combo boxes, contextual menus, tooltips and menus. Each begin
   that Nuklear pairs with an end records a scope, so the end can be
   checked against it and a widget against where it is placed. */

#include "ui_internal.h"

#include <math.h>

/* The flags a program may give a window or group: Nuklear's others are its
   own bookkeeping. */
#define UI_PANEL_FLAGS 2047

static bool ui_flags_ok(int64_t flags, const char *fn) {
    if (!(flags & ~(int64_t)UI_PANEL_FLAGS)) return true;
    return ui_misuse("%s: flags %lld are not a combination of the WINDOW_* flags", fn,
                     (long long)flags);
}

static bool ui_rect_ok(gs_ui_rect r, const char *fn) {
    if (isfinite(r.x) && isfinite(r.y) && isfinite(r.w) && isfinite(r.h)) return true;
    return ui_misuse("%s: a rectangle of %g, %g, %g, %g", fn, (double)r.x, (double)r.y,
                     (double)r.w, (double)r.h);
}

static bool ui_vec_ok(gs_ui_float2 v, const char *fn) {
    if (isfinite(v.x) && isfinite(v.y)) return true;
    return ui_misuse("%s: a size or position of %g, %g", fn, (double)v.x, (double)v.y);
}

static bool ui_float_ok(float f, const char *fn, const char *what) {
    if (isfinite(f)) return true;
    return ui_misuse("%s: %s %g", fn, what, (double)f);
}

/* The window of that name, as Nuklear finds one: by hash, then by name,
   ignoring case. */
static struct nk_window *ui_find_window(struct nk_context *ctx, const char *name) {
    int len = nk_strlen(name);
    nk_hash hash = nk_murmur_hash(name, len, NK_WINDOW_TITLE);
    for (struct nk_window *w = ctx->begin; w; w = w->next)
        if (w->name == hash && !nk_stricmpn(w->name_string, name, nk_strlen(w->name_string)))
            return w;
    return NULL;
}

/* --- windows ------------------------------------------------------------------- */

static uint8_t ui_begin(gs_ui_context c, gs_ui_bytes name, gs_ui_bytes title, gs_ui_rect bounds,
                        int64_t flags, const char *fn) {
    UI_CTX(c, fn, 0);
    if (u->in_input) return ui_misuse("%s between ui::input_begin and ui::input_end", fn);
    ui_scope *s = ui_top_scope(u);
    if (s)
        return ui_misuse("%s inside the %s begun by %s: windows do not nest", fn,
                         s->kind == UI_SCOPE_WINDOW ? "window" : "panel", s->begun_by);
    if (!ui_flags_ok(flags, fn) || !ui_rect_ok(bounds, fn)) return 0;
    const char *n = ui_cstr(name, 0);
    struct nk_window *win = ui_find_window(ctx, n);
    if (win && win->seq == ctx->seq)
        return ui_misuse("%s: the window \"%s\" was begun already this frame (a window's name "
                         "must be unique in a frame)", fn, n);
    nk_bool open = nk_begin_titled(ctx, n, ui_cstr(title, 1), ui_nk_rect(bounds), (nk_flags)flags);
    /* Ended whether it opened or not. */
    ui_push_scope(u, UI_SCOPE_WINDOW, open != 0, fn);
    u->drawn = true;
    return open != 0;
}

uint8_t gs_ui_begin(gs_ui_context c, gs_ui_bytes title, gs_ui_rect bounds, int64_t flags) {
    return ui_begin(c, title, title, bounds, flags, "ui::begin");
}

uint8_t gs_ui_begin_titled(gs_ui_context c, gs_ui_bytes name, gs_ui_bytes title,
                           gs_ui_rect bounds, int64_t flags) {
    return ui_begin(c, name, title, bounds, flags, "ui::begin_titled");
}

void gs_ui_end(gs_ui_context c) {
    UI_CTX(c, "ui::end", );
    if (u->in_input) {
        ui_misuse("ui::end between ui::input_begin and ui::input_end");
        return;
    }
    if (!ui_expect_scope(u, UI_SCOPE_WINDOW, "ui::end")) return;
    nk_end(ctx);
    ui_pop_scope(u);
}

uint8_t gs_ui_window_find(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_find", 0);
    return ui_find_window(ctx, ui_cstr(name, 0)) != NULL;
}

gs_ui_rect gs_ui_window_get_bounds(gs_ui_context c) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_WINDOW(c, "ui::window_get_bounds", none);
    return ui_rect_of(nk_window_get_bounds(ctx));
}

gs_ui_float2 gs_ui_window_get_position(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_WINDOW(c, "ui::window_get_position", none);
    return ui_float2_of(nk_window_get_position(ctx));
}

gs_ui_float2 gs_ui_window_get_size(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_WINDOW(c, "ui::window_get_size", none);
    return ui_float2_of(nk_window_get_size(ctx));
}

float gs_ui_window_get_width(gs_ui_context c) {
    UI_WINDOW(c, "ui::window_get_width", 0);
    return nk_window_get_width(ctx);
}

float gs_ui_window_get_height(gs_ui_context c) {
    UI_WINDOW(c, "ui::window_get_height", 0);
    return nk_window_get_height(ctx);
}

/* The current window, laid out: a hidden one has no layout to ask about. */
static bool ui_laid_out(ui_ctx *u, const char *fn) {
    if (!ui_in_window(u, fn)) return false;
    if (u->nk.current->layout) return true;
    return ui_misuse("%s: the window is hidden, so it has no contents to ask about", fn);
}

gs_ui_rect gs_ui_window_get_content_region(gs_ui_context c) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_CTX(c, "ui::window_get_content_region", none);
    if (!ui_laid_out(u, "ui::window_get_content_region")) return none;
    return ui_rect_of(nk_window_get_content_region(ctx));
}

gs_ui_float2 gs_ui_window_get_content_region_min(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_CTX(c, "ui::window_get_content_region_min", none);
    if (!ui_laid_out(u, "ui::window_get_content_region_min")) return none;
    return ui_float2_of(nk_window_get_content_region_min(ctx));
}

gs_ui_float2 gs_ui_window_get_content_region_max(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_CTX(c, "ui::window_get_content_region_max", none);
    if (!ui_laid_out(u, "ui::window_get_content_region_max")) return none;
    return ui_float2_of(nk_window_get_content_region_max(ctx));
}

gs_ui_float2 gs_ui_window_get_content_region_size(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_CTX(c, "ui::window_get_content_region_size", none);
    if (!ui_laid_out(u, "ui::window_get_content_region_size")) return none;
    return ui_float2_of(nk_window_get_content_region_size(ctx));
}

gs_ui_scroll gs_ui_window_get_scroll(gs_ui_context c) {
    gs_ui_scroll none = { 0, 0 };
    UI_WINDOW(c, "ui::window_get_scroll", none);
    nk_uint x = 0, y = 0;
    nk_window_get_scroll(ctx, &x, &y);
    return ui_scroll_of(x, y);
}

uint8_t gs_ui_window_has_focus(gs_ui_context c) {
    UI_CTX(c, "ui::window_has_focus", 0);
    return ui_laid_out(u, "ui::window_has_focus") && nk_window_has_focus(ctx);
}

uint8_t gs_ui_window_is_hovered(gs_ui_context c) {
    UI_CTX(c, "ui::window_is_hovered", 0);
    return ui_laid_out(u, "ui::window_is_hovered") && nk_window_is_hovered(ctx);
}

uint8_t gs_ui_window_is_collapsed(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_is_collapsed", 0);
    return nk_window_is_collapsed(ctx, ui_cstr(name, 0)) != 0;
}

uint8_t gs_ui_window_is_closed(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_is_closed", 1);
    return nk_window_is_closed(ctx, ui_cstr(name, 0)) != 0;
}

uint8_t gs_ui_window_is_hidden(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_is_hidden", 1);
    return nk_window_is_hidden(ctx, ui_cstr(name, 0)) != 0;
}

uint8_t gs_ui_window_is_active(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_is_active", 0);
    return nk_window_is_active(ctx, ui_cstr(name, 0)) != 0;
}

uint8_t gs_ui_window_is_any_hovered(gs_ui_context c) {
    UI_CTX(c, "ui::window_is_any_hovered", 0);
    return nk_window_is_any_hovered(ctx) != 0;
}

uint8_t gs_ui_item_is_any_active(gs_ui_context c) {
    UI_CTX(c, "ui::item_is_any_active", 0);
    return nk_item_is_any_active(ctx) != 0;
}

void gs_ui_window_set_bounds(gs_ui_context c, gs_ui_bytes name, gs_ui_rect bounds) {
    UI_CTX(c, "ui::window_set_bounds", );
    if (ui_rect_ok(bounds, "ui::window_set_bounds"))
        nk_window_set_bounds(ctx, ui_cstr(name, 0), ui_nk_rect(bounds));
}

void gs_ui_window_set_position(gs_ui_context c, gs_ui_bytes name, gs_ui_float2 pos) {
    UI_CTX(c, "ui::window_set_position", );
    if (ui_vec_ok(pos, "ui::window_set_position"))
        nk_window_set_position(ctx, ui_cstr(name, 0), ui_nk_vec2(pos));
}

void gs_ui_window_set_size(gs_ui_context c, gs_ui_bytes name, gs_ui_float2 size) {
    UI_CTX(c, "ui::window_set_size", );
    if (ui_vec_ok(size, "ui::window_set_size"))
        nk_window_set_size(ctx, ui_cstr(name, 0), ui_nk_vec2(size));
}

void gs_ui_window_set_focus(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_set_focus", );
    nk_window_set_focus(ctx, ui_cstr(name, 0));
}

void gs_ui_window_set_scroll(gs_ui_context c, int64_t x, int64_t y) {
    UI_WINDOW(c, "ui::window_set_scroll", );
    nk_window_set_scroll(ctx, (nk_uint)(x < 0 ? 0 : x > UINT32_MAX ? UINT32_MAX : x),
                         (nk_uint)(y < 0 ? 0 : y > UINT32_MAX ? UINT32_MAX : y));
}

void gs_ui_window_close(gs_ui_context c, gs_ui_bytes name) {
    UI_CTX(c, "ui::window_close", );
    const char *n = ui_cstr(name, 0);
    struct nk_window *win = ui_find_window(ctx, n);
    if (win && win == ctx->current) {
        ui_misuse("ui::window_close: \"%s\" is the window being built; close it after ui::end",
                  n);
        return;
    }
    nk_window_close(ctx, n);
}

void gs_ui_window_collapse(gs_ui_context c, gs_ui_bytes name, int64_t state) {
    UI_CTX(c, "ui::window_collapse", );
    if (ui_enum_ok(state, 2, "ui::window_collapse", "MINIMIZED, MAXIMIZED"))
        nk_window_collapse(ctx, ui_cstr(name, 0), (enum nk_collapse_states)state);
}

void gs_ui_window_collapse_if(gs_ui_context c, gs_ui_bytes name, int64_t state, uint8_t cond) {
    UI_CTX(c, "ui::window_collapse_if", );
    if (ui_enum_ok(state, 2, "ui::window_collapse_if", "MINIMIZED, MAXIMIZED"))
        nk_window_collapse_if(ctx, ui_cstr(name, 0), (enum nk_collapse_states)state, cond != 0);
}

void gs_ui_window_show(gs_ui_context c, gs_ui_bytes name, int64_t state) {
    UI_CTX(c, "ui::window_show", );
    if (ui_enum_ok(state, 2, "ui::window_show", "HIDDEN, SHOWN"))
        nk_window_show(ctx, ui_cstr(name, 0), (enum nk_show_states)state);
}

void gs_ui_window_show_if(gs_ui_context c, gs_ui_bytes name, int64_t state, uint8_t cond) {
    UI_CTX(c, "ui::window_show_if", );
    if (ui_enum_ok(state, 2, "ui::window_show_if", "HIDDEN, SHOWN"))
        nk_window_show_if(ctx, ui_cstr(name, 0), (enum nk_show_states)state, cond != 0);
}

void gs_ui_rule_horizontal(gs_ui_context c, gs_ui_color color, uint8_t rounding) {
    UI_PANEL(c, "ui::rule_horizontal", );
    nk_rule_horizontal(ctx, ui_nk_color(color), rounding != 0);
}

/* --- layout ---------------------------------------------------------------------- */

/* A new row's layout cannot start while one begun by layout_row_begin, a
   row template or a layout space is still open. */
static bool ui_new_row(ui_ctx *u, const char *fn) {
    if (!ui_in_panel(u, fn)) return false;
    ui_scope *s = ui_top_scope(u);
    if (s->kind == UI_SCOPE_ROW || s->kind == UI_SCOPE_SPACE)
        return ui_expect_scope(u, UI_SCOPE_WINDOW, fn);    /* says what is open */
    return true;
}

static bool ui_row_height_ok(float height, const char *fn) {
    if (isfinite(height) && height >= 0) return true;
    return ui_misuse("%s: a row %g high", fn, (double)height);
}

static bool ui_cols_ok(int64_t cols, const char *fn) {
    if (cols >= 1 && cols <= 1024) return true;
    return ui_misuse("%s: %lld columns (1 to 1024)", fn, (long long)cols);
}

void gs_ui_layout_set_min_row_height(gs_ui_context c, float height) {
    UI_PANEL(c, "ui::layout_set_min_row_height", );
    if (ui_row_height_ok(height, "ui::layout_set_min_row_height"))
        nk_layout_set_min_row_height(ctx, height);
}

void gs_ui_layout_reset_min_row_height(gs_ui_context c) {
    UI_PANEL(c, "ui::layout_reset_min_row_height", );
    nk_layout_reset_min_row_height(ctx);
}

gs_ui_rect gs_ui_layout_widget_bounds(gs_ui_context c) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_PANEL(c, "ui::layout_widget_bounds", none);
    return ui_rect_of(nk_layout_widget_bounds(ctx));
}

float gs_ui_layout_ratio_from_pixel(gs_ui_context c, float pixel_width) {
    UI_PANEL(c, "ui::layout_ratio_from_pixel", 0);
    if (!ui_float_ok(pixel_width, "ui::layout_ratio_from_pixel", "a width of")) return 0;
    return nk_layout_ratio_from_pixel(ctx, pixel_width);
}

void gs_ui_layout_row_dynamic(gs_ui_context c, float height, int64_t cols) {
    UI_CTX(c, "ui::layout_row_dynamic", );
    if (ui_new_row(u, "ui::layout_row_dynamic") && ui_row_height_ok(height, "ui::layout_row_dynamic") &&
        ui_cols_ok(cols, "ui::layout_row_dynamic"))
        nk_layout_row_dynamic(ctx, height, (int)cols);
}

void gs_ui_layout_row_static(gs_ui_context c, float height, int64_t item_width, int64_t cols) {
    UI_CTX(c, "ui::layout_row_static", );
    if (ui_new_row(u, "ui::layout_row_static") && ui_row_height_ok(height, "ui::layout_row_static") &&
        ui_cols_ok(cols, "ui::layout_row_static"))
        nk_layout_row_static(ctx, height, ui_int(item_width), (int)cols);
}

void gs_ui_layout_row_begin(gs_ui_context c, int64_t format, float row_height, int64_t cols) {
    UI_CTX(c, "ui::layout_row_begin", );
    if (!ui_new_row(u, "ui::layout_row_begin") ||
        !ui_enum_ok(format, 2, "ui::layout_row_begin", "DYNAMIC, STATIC") ||
        !ui_row_height_ok(row_height, "ui::layout_row_begin") ||
        !ui_cols_ok(cols, "ui::layout_row_begin") ||
        !ui_push_scope(u, UI_SCOPE_ROW, true, "ui::layout_row_begin"))
        return;
    nk_layout_row_begin(ctx, (enum nk_layout_format)format, row_height, (int)cols);
}

void gs_ui_layout_row_push(gs_ui_context c, float value) {
    UI_PANEL(c, "ui::layout_row_push", );
    if (ui_expect_scope(u, UI_SCOPE_ROW, "ui::layout_row_push") &&
        ui_float_ok(value, "ui::layout_row_push", "a width or ratio of"))
        nk_layout_row_push(ctx, value);
}

void gs_ui_layout_row_end(gs_ui_context c) {
    UI_PANEL(c, "ui::layout_row_end", );
    if (!ui_expect_scope(u, UI_SCOPE_ROW, "ui::layout_row_end")) return;
    nk_layout_row_end(ctx);
    ui_pop_scope(u);
}

void gs_ui_layout_row(gs_ui_context c, int64_t format, float height, gs_ui_f32_slice ratios) {
    UI_CTX(c, "ui::layout_row", );
    if (!ui_new_row(u, "ui::layout_row") ||
        !ui_enum_ok(format, 2, "ui::layout_row", "DYNAMIC, STATIC") ||
        !ui_row_height_ok(height, "ui::layout_row") || !ui_cols_ok(ratios.len, "ui::layout_row"))
        return;
    for (int64_t i = 0; i < ratios.len; i++)
        if (!ui_float_ok(ratios.data[i], "ui::layout_row", "a ratio or width of")) return;
    /* Nuklear reads the ratios as the row's widgets are placed. */
    float *copy = ui_frame_floats(u, ratios.len);
    memcpy(copy, ratios.data, sizeof(float) * (size_t)ratios.len);
    nk_layout_row(ctx, (enum nk_layout_format)format, height, (int)ratios.len, copy);
}

void gs_ui_layout_row_template_begin(gs_ui_context c, float row_height) {
    UI_CTX(c, "ui::layout_row_template_begin", );
    if (!ui_new_row(u, "ui::layout_row_template_begin") ||
        !ui_row_height_ok(row_height, "ui::layout_row_template_begin") ||
        !ui_push_scope(u, UI_SCOPE_TEMPLATE, true, "ui::layout_row_template_begin"))
        return;
    nk_layout_row_template_begin(ctx, row_height);
}

/* A template column, up to Nuklear's 16. */
static bool ui_template_push(ui_ctx *u, const char *fn) {
    if (!ui_in_window(u, fn) || !ui_expect_scope(u, UI_SCOPE_TEMPLATE, fn)) return false;
    if (u->nk.current->layout->row.columns < NK_MAX_LAYOUT_ROW_TEMPLATE_COLUMNS) return true;
    return ui_misuse("%s: a row template has at most %d columns", fn,
                     NK_MAX_LAYOUT_ROW_TEMPLATE_COLUMNS);
}

void gs_ui_layout_row_template_push_dynamic(gs_ui_context c) {
    UI_CTX(c, "ui::layout_row_template_push_dynamic", );
    if (ui_template_push(u, "ui::layout_row_template_push_dynamic"))
        nk_layout_row_template_push_dynamic(ctx);
}

void gs_ui_layout_row_template_push_variable(gs_ui_context c, float min_width) {
    UI_CTX(c, "ui::layout_row_template_push_variable", );
    if (ui_template_push(u, "ui::layout_row_template_push_variable") &&
        ui_float_ok(min_width, "ui::layout_row_template_push_variable", "a width of"))
        nk_layout_row_template_push_variable(ctx, min_width);
}

void gs_ui_layout_row_template_push_static(gs_ui_context c, float width) {
    UI_CTX(c, "ui::layout_row_template_push_static", );
    if (ui_template_push(u, "ui::layout_row_template_push_static") &&
        ui_float_ok(width, "ui::layout_row_template_push_static", "a width of"))
        nk_layout_row_template_push_static(ctx, width);
}

void gs_ui_layout_row_template_end(gs_ui_context c) {
    UI_CTX(c, "ui::layout_row_template_end", );
    if (!ui_in_window(u, "ui::layout_row_template_end") ||
        !ui_expect_scope(u, UI_SCOPE_TEMPLATE, "ui::layout_row_template_end"))
        return;
    nk_layout_row_template_end(ctx);
    ui_pop_scope(u);
}

void gs_ui_layout_space_begin(gs_ui_context c, int64_t format, float height, int64_t widget_count) {
    UI_CTX(c, "ui::layout_space_begin", );
    if (!ui_new_row(u, "ui::layout_space_begin") ||
        !ui_enum_ok(format, 2, "ui::layout_space_begin", "DYNAMIC, STATIC") ||
        !ui_row_height_ok(height, "ui::layout_space_begin"))
        return;
    if (widget_count < 0) {
        ui_misuse("ui::layout_space_begin: %lld widgets", (long long)widget_count);
        return;
    }
    if (!ui_push_scope(u, UI_SCOPE_SPACE, true, "ui::layout_space_begin")) return;
    nk_layout_space_begin(ctx, (enum nk_layout_format)format, height, ui_int(widget_count));
}

void gs_ui_layout_space_push(gs_ui_context c, gs_ui_rect bounds) {
    UI_PANEL(c, "ui::layout_space_push", );
    if (ui_expect_scope(u, UI_SCOPE_SPACE, "ui::layout_space_push") &&
        ui_rect_ok(bounds, "ui::layout_space_push"))
        nk_layout_space_push(ctx, ui_nk_rect(bounds));
}

void gs_ui_layout_space_end(gs_ui_context c) {
    UI_PANEL(c, "ui::layout_space_end", );
    if (!ui_expect_scope(u, UI_SCOPE_SPACE, "ui::layout_space_end")) return;
    nk_layout_space_end(ctx);
    ui_pop_scope(u);
}

gs_ui_rect gs_ui_layout_space_bounds(gs_ui_context c) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_PANEL(c, "ui::layout_space_bounds", none);
    return ui_rect_of(nk_layout_space_bounds(ctx));
}

gs_ui_float2 gs_ui_layout_space_to_screen(gs_ui_context c, gs_ui_float2 v) {
    gs_ui_float2 none = { 0, 0 };
    UI_PANEL(c, "ui::layout_space_to_screen", none);
    return ui_float2_of(nk_layout_space_to_screen(ctx, ui_nk_vec2(v)));
}

gs_ui_float2 gs_ui_layout_space_to_local(gs_ui_context c, gs_ui_float2 v) {
    gs_ui_float2 none = { 0, 0 };
    UI_PANEL(c, "ui::layout_space_to_local", none);
    return ui_float2_of(nk_layout_space_to_local(ctx, ui_nk_vec2(v)));
}

gs_ui_rect gs_ui_layout_space_rect_to_screen(gs_ui_context c, gs_ui_rect r) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_PANEL(c, "ui::layout_space_rect_to_screen", none);
    return ui_rect_of(nk_layout_space_rect_to_screen(ctx, ui_nk_rect(r)));
}

gs_ui_rect gs_ui_layout_space_rect_to_local(gs_ui_context c, gs_ui_rect r) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_PANEL(c, "ui::layout_space_rect_to_local", none);
    return ui_rect_of(nk_layout_space_rect_to_local(ctx, ui_nk_rect(r)));
}

void gs_ui_spacer(gs_ui_context c) {
    UI_PANEL(c, "ui::spacer", );
    nk_spacer(ctx);
}

/* --- groups ------------------------------------------------------------------------ */

/* Nuklear's group begin returns 1 for an open group, 0 for one out of view
   (nothing to end), and for a minimized or closed one the flag saying so,
   having ended it already. */
static uint8_t ui_group_opened(ui_ctx *u, nk_bool r, enum ui_scope_kind kind, const char *fn) {
    if (r != 1) return 0;
    return ui_push_scope(u, kind, true, fn);
}

uint8_t gs_ui_group_begin(gs_ui_context c, gs_ui_bytes title, int64_t flags) {
    UI_PANEL(c, "ui::group_begin", 0);
    if (!ui_flags_ok(flags, "ui::group_begin")) return 0;
    return ui_group_opened(u, nk_group_begin(ctx, ui_cstr(title, 0), (nk_flags)flags),
                           UI_SCOPE_GROUP, "ui::group_begin");
}

uint8_t gs_ui_group_begin_titled(gs_ui_context c, gs_ui_bytes id, gs_ui_bytes title,
                                 int64_t flags) {
    UI_PANEL(c, "ui::group_begin_titled", 0);
    if (!ui_flags_ok(flags, "ui::group_begin_titled")) return 0;
    return ui_group_opened(
        u, nk_group_begin_titled(ctx, ui_cstr(id, 0), ui_cstr(title, 1), (nk_flags)flags),
        UI_SCOPE_GROUP, "ui::group_begin_titled");
}

void gs_ui_group_end(gs_ui_context c) {
    UI_CTX(c, "ui::group_end", );
    if (!ui_in_window(u, "ui::group_end") || !ui_expect_scope(u, UI_SCOPE_GROUP, "ui::group_end"))
        return;
    nk_group_end(ctx);
    ui_pop_scope(u);
}

uint8_t gs_ui_group_scrolled_begin(gs_ui_context c, gs_ui_scroll offset, gs_ui_bytes title,
                                   int64_t flags) {
    UI_PANEL(c, "ui::group_scrolled_begin", 0);
    if (!ui_flags_ok(flags, "ui::group_scrolled_begin") ||
        !ui_push_scope(u, UI_SCOPE_GROUP_SCROLLED, true, "ui::group_scrolled_begin"))
        return 0;
    /* The offsets live in the scope, where Nuklear writes them back when the
       group ends; ui::group_scrolled_end hands them to the program. */
    ui_scope *s = ui_top_scope(u);
    s->scroll_x = offset.x;
    s->scroll_y = offset.y;
    nk_bool r = nk_group_scrolled_offset_begin(ctx, &s->scroll_x, &s->scroll_y, ui_cstr(title, 0),
                                               (nk_flags)flags);
    if (r != 1) {
        ui_pop_scope(u);
        return 0;
    }
    return 1;
}

gs_ui_scroll gs_ui_group_scrolled_end(gs_ui_context c) {
    gs_ui_scroll none = { 0, 0 };
    UI_CTX(c, "ui::group_scrolled_end", none);
    if (!ui_in_window(u, "ui::group_scrolled_end") ||
        !ui_expect_scope(u, UI_SCOPE_GROUP_SCROLLED, "ui::group_scrolled_end"))
        return none;
    nk_group_scrolled_end(ctx);
    ui_scope *s = ui_top_scope(u);
    gs_ui_scroll offset = ui_scroll_of(s->scroll_x, s->scroll_y);
    ui_pop_scope(u);
    return offset;
}

gs_ui_scroll gs_ui_group_get_scroll(gs_ui_context c, gs_ui_bytes id) {
    gs_ui_scroll none = { 0, 0 };
    UI_PANEL(c, "ui::group_get_scroll", none);
    nk_uint x = 0, y = 0;
    nk_group_get_scroll(ctx, ui_cstr(id, 0), &x, &y);
    return ui_scroll_of(x, y);
}

void gs_ui_group_set_scroll(gs_ui_context c, gs_ui_bytes id, int64_t x, int64_t y) {
    UI_PANEL(c, "ui::group_set_scroll", );
    nk_group_set_scroll(ctx, ui_cstr(id, 0), (nk_uint)(x < 0 ? 0 : x > UINT32_MAX ? UINT32_MAX : x),
                        (nk_uint)(y < 0 ? 0 : y > UINT32_MAX ? UINT32_MAX : y));
}

/* --- trees -------------------------------------------------------------------------- */

static bool ui_tree_args_ok(int64_t type, int64_t state, const char *fn) {
    return ui_enum_ok(type, 2, fn, "TREE_NODE, TREE_TAB") &&
           ui_enum_ok(state, 2, fn, "MINIMIZED, MAXIMIZED");
}

/* Nuklear keys a tree node's state on a hash of `hash` and `seed`, or of
   the title when there is no hash. */
static const char *ui_tree_hash(gs_ui_bytes hash) {
    return hash.len ? ui_cstr(hash, 1) : NULL;
}

uint8_t gs_ui_tree_push_hashed(gs_ui_context c, int64_t type, gs_ui_bytes title,
                               int64_t initial_state, gs_ui_bytes hash, int64_t seed) {
    UI_CTX(c, "ui::tree_push_hashed", 0);
    if (!ui_new_row(u, "ui::tree_push_hashed") ||
        !ui_tree_args_ok(type, initial_state, "ui::tree_push_hashed"))
        return 0;
    nk_bool r = nk_tree_push_hashed(ctx, (enum nk_tree_type)type, ui_cstr(title, 0),
                                    (enum nk_collapse_states)initial_state, ui_tree_hash(hash),
                                    ui_len(hash, "ui::tree_push_hashed"), (int)seed);
    return r && ui_push_scope(u, UI_SCOPE_TREE, true, "ui::tree_push_hashed");
}

uint8_t gs_ui_tree_image_push_hashed(gs_ui_context c, int64_t type, gs_ui_image img,
                                     gs_ui_bytes title, int64_t initial_state, gs_ui_bytes hash,
                                     int64_t seed) {
    UI_CTX(c, "ui::tree_image_push_hashed", 0);
    if (!ui_new_row(u, "ui::tree_image_push_hashed") ||
        !ui_tree_args_ok(type, initial_state, "ui::tree_image_push_hashed"))
        return 0;
    nk_bool r = nk_tree_image_push_hashed(ctx, (enum nk_tree_type)type, ui_nk_image(img),
                                          ui_cstr(title, 0), (enum nk_collapse_states)initial_state,
                                          ui_tree_hash(hash),
                                          ui_len(hash, "ui::tree_image_push_hashed"), (int)seed);
    return r && ui_push_scope(u, UI_SCOPE_TREE, true, "ui::tree_image_push_hashed");
}

void gs_ui_tree_pop(gs_ui_context c) {
    UI_PANEL(c, "ui::tree_pop", );
    if (!ui_expect_scope(u, UI_SCOPE_TREE, "ui::tree_pop")) return;
    nk_tree_pop(ctx);
    ui_pop_scope(u);
}

static uint8_t ui_tree_state_push(gs_ui_context c, int64_t type, const gs_ui_image *img,
                                  gs_ui_bytes title, int64_t *state, const char *fn) {
    UI_CTX(c, fn, 0);
    if (!ui_new_row(u, fn) || !ui_tree_args_ok(type, *state, fn)) return 0;
    enum nk_collapse_states s = (enum nk_collapse_states)*state;
    nk_bool r = img ? nk_tree_state_image_push(ctx, (enum nk_tree_type)type, ui_nk_image(*img),
                                               ui_cstr(title, 0), &s)
                    : nk_tree_state_push(ctx, (enum nk_tree_type)type, ui_cstr(title, 0), &s);
    *state = s;
    return r && ui_push_scope(u, UI_SCOPE_TREE_STATE, true, fn);
}

uint8_t gs_ui_tree_state_push(gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t *state) {
    return ui_tree_state_push(c, type, NULL, title, state, "ui::tree_state_push");
}

uint8_t gs_ui_tree_state_image_push(gs_ui_context c, int64_t type, gs_ui_image img,
                                    gs_ui_bytes title, int64_t *state) {
    return ui_tree_state_push(c, type, &img, title, state, "ui::tree_state_image_push");
}

void gs_ui_tree_state_pop(gs_ui_context c) {
    UI_PANEL(c, "ui::tree_state_pop", );
    if (!ui_expect_scope(u, UI_SCOPE_TREE_STATE, "ui::tree_state_pop")) return;
    nk_tree_state_pop(ctx);
    ui_pop_scope(u);
}

static uint8_t ui_tree_element_push(gs_ui_context c, int64_t type, const gs_ui_image *img,
                                    gs_ui_bytes title, int64_t initial_state, uint8_t *selected,
                                    gs_ui_bytes hash, int64_t seed, const char *fn) {
    UI_CTX(c, fn, 0);
    if (!ui_new_row(u, fn) || !ui_tree_args_ok(type, initial_state, fn)) return 0;
    nk_bool sel = *selected != 0;
    nk_bool r = img ? nk_tree_element_image_push_hashed(
                          ctx, (enum nk_tree_type)type, ui_nk_image(*img), ui_cstr(title, 0),
                          (enum nk_collapse_states)initial_state, &sel, ui_tree_hash(hash),
                          ui_len(hash, fn), (int)seed)
                    : nk_tree_element_push_hashed(ctx, (enum nk_tree_type)type, ui_cstr(title, 0),
                                                  (enum nk_collapse_states)initial_state, &sel,
                                                  ui_tree_hash(hash), ui_len(hash, fn), (int)seed);
    *selected = sel != 0;
    return r && ui_push_scope(u, UI_SCOPE_TREE_ELEMENT, true, fn);
}

uint8_t gs_ui_tree_element_push_hashed(gs_ui_context c, int64_t type, gs_ui_bytes title,
                                       int64_t initial_state, uint8_t *selected, gs_ui_bytes hash,
                                       int64_t seed) {
    return ui_tree_element_push(c, type, NULL, title, initial_state, selected, hash, seed,
                                "ui::tree_element_push_hashed");
}

uint8_t gs_ui_tree_element_image_push_hashed(gs_ui_context c, int64_t type, gs_ui_image img,
                                             gs_ui_bytes title, int64_t initial_state,
                                             uint8_t *selected, gs_ui_bytes hash, int64_t seed) {
    return ui_tree_element_push(c, type, &img, title, initial_state, selected, hash, seed,
                                "ui::tree_element_image_push_hashed");
}

void gs_ui_tree_element_pop(gs_ui_context c) {
    UI_PANEL(c, "ui::tree_element_pop", );
    if (!ui_expect_scope(u, UI_SCOPE_TREE_ELEMENT, "ui::tree_element_pop")) return;
    nk_tree_element_pop(ctx);
    ui_pop_scope(u);
}

/* --- list views ---------------------------------------------------------------------- */

uint8_t gs_ui_list_view_begin(gs_ui_context c, gs_ui_list_view *out, gs_ui_bytes id,
                              int64_t flags, int64_t row_height, int64_t row_count) {
    memset(out, 0, sizeof *out);
    UI_PANEL(c, "ui::list_view_begin", 0);
    if (!ui_flags_ok(flags, "ui::list_view_begin")) return 0;
    if (row_height < 1 || row_height > 65536 || row_count < 0 ||
        row_count > INT32_MAX / row_height) {
        ui_misuse("ui::list_view_begin: %lld rows %lld high", (long long)row_count,
                  (long long)row_height);
        return 0;
    }
    if (!ui_push_scope(u, UI_SCOPE_LIST_VIEW, true, "ui::list_view_begin")) return 0;
    /* The view lives in the scope: its end needs it. */
    ui_scope *s = ui_top_scope(u);
    nk_bool r = nk_list_view_begin(ctx, &s->view, ui_cstr(id, 0), (nk_flags)flags, (int)row_height,
                                   (int)row_count);
    if (r != 1) {
        ui_pop_scope(u);
        return 0;
    }
    out->begin = s->view.begin;
    out->end = s->view.end;
    out->count = s->view.count;
    return 1;
}

void gs_ui_list_view_end(gs_ui_context c) {
    UI_CTX(c, "ui::list_view_end", );
    if (!ui_in_window(u, "ui::list_view_end") ||
        !ui_expect_scope(u, UI_SCOPE_LIST_VIEW, "ui::list_view_end"))
        return;
    nk_list_view_end(&ui_top_scope(u)->view);
    ui_pop_scope(u);
}

/* --- popups ------------------------------------------------------------------------- */

/* What a close or item inside a popup-like panel of `kind` checks: that the
   innermost panel is one. */
static bool ui_inside(ui_ctx *u, enum ui_scope_kind kind, const char *fn) {
    if (!ui_in_panel(u, fn)) return false;
    for (int i = u->nscopes - 1; i >= 0; i--) {
        uint8_t k = u->scopes[i].kind;
        if (k == kind) return true;
        if (k <= UI_SCOPE_TOOLTIP) break;
    }
    return ui_expect_scope(u, kind, fn);    /* says what is open instead */
}

/* A popup-like panel begun: the checks before, and the scope after. */
static bool ui_popup_ok(ui_ctx *u, const char *fn) {
    return ui_in_panel(u, fn) && ui_popup_allowed(u, fn);
}

static uint8_t ui_opened(ui_ctx *u, nk_bool r, enum ui_scope_kind kind, const char *fn) {
    return r && ui_push_scope(u, kind, true, fn);
}

uint8_t gs_ui_popup_begin(gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t flags,
                          gs_ui_rect rect) {
    UI_CTX(c, "ui::popup_begin", 0);
    if (!ui_popup_ok(u, "ui::popup_begin") ||
        !ui_enum_ok(type, 2, "ui::popup_begin", "POPUP_STATIC, POPUP_DYNAMIC") ||
        !ui_flags_ok(flags, "ui::popup_begin") || !ui_rect_ok(rect, "ui::popup_begin"))
        return 0;
    return ui_opened(u, nk_popup_begin(ctx, (enum nk_popup_type)type, ui_cstr(title, 0),
                                       (nk_flags)flags, ui_nk_rect(rect)),
                     UI_SCOPE_POPUP, "ui::popup_begin");
}

void gs_ui_popup_close(gs_ui_context c) {
    UI_CTX(c, "ui::popup_close", );
    if (ui_inside(u, UI_SCOPE_POPUP, "ui::popup_close")) nk_popup_close(ctx);
}

void gs_ui_popup_end(gs_ui_context c) {
    UI_CTX(c, "ui::popup_end", );
    if (!ui_in_window(u, "ui::popup_end") || !ui_expect_scope(u, UI_SCOPE_POPUP, "ui::popup_end"))
        return;
    nk_popup_end(ctx);
    ui_pop_scope(u);
}

gs_ui_scroll gs_ui_popup_get_scroll(gs_ui_context c) {
    gs_ui_scroll none = { 0, 0 };
    UI_CTX(c, "ui::popup_get_scroll", none);
    if (!ui_inside(u, UI_SCOPE_POPUP, "ui::popup_get_scroll")) return none;
    nk_uint x = 0, y = 0;
    nk_popup_get_scroll(ctx, &x, &y);
    return ui_scroll_of(x, y);
}

void gs_ui_popup_set_scroll(gs_ui_context c, int64_t x, int64_t y) {
    UI_CTX(c, "ui::popup_set_scroll", );
    if (ui_inside(u, UI_SCOPE_POPUP, "ui::popup_set_scroll"))
        nk_popup_set_scroll(ctx, (nk_uint)(x < 0 ? 0 : x > UINT32_MAX ? UINT32_MAX : x),
                            (nk_uint)(y < 0 ? 0 : y > UINT32_MAX ? UINT32_MAX : y));
}

/* --- combo boxes -------------------------------------------------------------------- */

static bool ui_combo_size_ok(gs_ui_float2 size, const char *fn) {
    return ui_vec_ok(size, fn);
}

/* The separated items as Nuklear walks them: it reads `count` of them,
   and the one at `selected`, past the string's end if there are fewer. */
static const char *ui_combo_items(gs_ui_bytes items, int64_t separator, int64_t selected,
                                  int64_t count, int64_t item_height, const char *fn) {
    if (separator < 0 || separator > 127) {
        ui_misuse("%s: a separator %lld, where it is an ASCII character", fn, (long long)separator);
        return NULL;
    }
    /* Nuklear ends an item at the separator or a zero byte, whichever comes
       first: with zero as the separator, the items are a string's parts. */
    const char *s = ui_cstr(items, 0);
    int64_t have = 1;
    for (int64_t i = 0; i < items.len; i++) have += !s[i] || s[i] == (char)separator;
    if (count < 1 || count > have) {
        ui_misuse("%s: %lld items, where the string holds %lld", fn, (long long)count,
                  (long long)have);
        return NULL;
    }
    if (selected < 0 || selected >= count) {
        ui_misuse("%s: item %lld selected, of %lld", fn, (long long)selected, (long long)count);
        return NULL;
    }
    if (item_height < 1 || item_height > 65536) {
        ui_misuse("%s: items %lld high", fn, (long long)item_height);
        return NULL;
    }
    return s;
}

int64_t gs_ui_combo_separator(gs_ui_context c, gs_ui_bytes items, int64_t separator,
                              int64_t selected, int64_t count, int64_t item_height,
                              gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_separator", selected);
    if (!ui_popup_ok(u, "ui::combo_separator") ||
        !ui_combo_size_ok(size, "ui::combo_separator"))
        return selected;
    const char *s = ui_combo_items(items, separator, selected, count, item_height,
                                   "ui::combo_separator");
    if (!s) return selected;
    return nk_combo_separator(ctx, s, (int)separator, (int)selected, (int)count, (int)item_height,
                              ui_nk_vec2(size));
}

uint8_t gs_ui_combobox_separator(gs_ui_context c, gs_ui_bytes items, int64_t separator,
                                 int64_t *selected, int64_t count, int64_t item_height,
                                 gs_ui_float2 size) {
    UI_CTX(c, "ui::combobox_separator", 0);
    if (!ui_popup_ok(u, "ui::combobox_separator") ||
        !ui_combo_size_ok(size, "ui::combobox_separator"))
        return 0;
    const char *s = ui_combo_items(items, separator, *selected, count, item_height,
                                   "ui::combobox_separator");
    if (!s) return 0;
    int sel = (int)*selected;
    nk_bool r = nk_combobox_separator(ctx, s, (int)separator, &sel, (int)count, (int)item_height,
                                      ui_nk_vec2(size));
    *selected = sel;
    return r != 0;
}

uint8_t gs_ui_combo_begin_label(gs_ui_context c, gs_ui_bytes selected, gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_label", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_label") || !ui_combo_size_ok(size, "ui::combo_begin_label"))
        return 0;
    return ui_opened(u, nk_combo_begin_text(ctx, (const char *)selected.data,
                                            ui_len(selected, "ui::combo_begin_label"),
                                            ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_label");
}

uint8_t gs_ui_combo_begin_color(gs_ui_context c, gs_ui_color color, gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_color", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_color") || !ui_combo_size_ok(size, "ui::combo_begin_color"))
        return 0;
    return ui_opened(u, nk_combo_begin_color(ctx, ui_nk_color(color), ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_color");
}

uint8_t gs_ui_combo_begin_symbol(gs_ui_context c, int64_t symbol, gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_symbol", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_symbol") || !ui_symbol_ok(symbol, "ui::combo_begin_symbol") ||
        !ui_combo_size_ok(size, "ui::combo_begin_symbol"))
        return 0;
    return ui_opened(u, nk_combo_begin_symbol(ctx, (enum nk_symbol_type)symbol, ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_symbol");
}

uint8_t gs_ui_combo_begin_symbol_label(gs_ui_context c, gs_ui_bytes selected, int64_t symbol,
                                       gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_symbol_label", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_symbol_label") ||
        !ui_symbol_ok(symbol, "ui::combo_begin_symbol_label") ||
        !ui_combo_size_ok(size, "ui::combo_begin_symbol_label"))
        return 0;
    return ui_opened(u, nk_combo_begin_symbol_text(ctx, (const char *)selected.data,
                                                   ui_len(selected, "ui::combo_begin_symbol_label"),
                                                   (enum nk_symbol_type)symbol, ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_symbol_label");
}

uint8_t gs_ui_combo_begin_image(gs_ui_context c, gs_ui_image img, gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_image", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_image") || !ui_combo_size_ok(size, "ui::combo_begin_image"))
        return 0;
    return ui_opened(u, nk_combo_begin_image(ctx, ui_nk_image(img), ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_image");
}

uint8_t gs_ui_combo_begin_image_label(gs_ui_context c, gs_ui_bytes selected, gs_ui_image img,
                                      gs_ui_float2 size) {
    UI_CTX(c, "ui::combo_begin_image_label", 0);
    if (!ui_popup_ok(u, "ui::combo_begin_image_label") ||
        !ui_combo_size_ok(size, "ui::combo_begin_image_label"))
        return 0;
    return ui_opened(u, nk_combo_begin_image_text(ctx, (const char *)selected.data,
                                                  ui_len(selected, "ui::combo_begin_image_label"),
                                                  ui_nk_image(img), ui_nk_vec2(size)),
                     UI_SCOPE_COMBO, "ui::combo_begin_image_label");
}

uint8_t gs_ui_combo_item_label(gs_ui_context c, gs_ui_bytes text, int64_t align) {
    UI_CTX(c, "ui::combo_item_label", 0);
    return ui_inside(u, UI_SCOPE_COMBO, "ui::combo_item_label") &&
           nk_combo_item_text(ctx, (const char *)text.data, ui_len(text, "ui::combo_item_label"),
                              (nk_flags)align);
}

uint8_t gs_ui_combo_item_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                     int64_t align) {
    UI_CTX(c, "ui::combo_item_image_label", 0);
    return ui_inside(u, UI_SCOPE_COMBO, "ui::combo_item_image_label") &&
           nk_combo_item_image_text(ctx, ui_nk_image(img), (const char *)text.data,
                                    ui_len(text, "ui::combo_item_image_label"), (nk_flags)align);
}

uint8_t gs_ui_combo_item_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                      int64_t align) {
    UI_CTX(c, "ui::combo_item_symbol_label", 0);
    return ui_inside(u, UI_SCOPE_COMBO, "ui::combo_item_symbol_label") &&
           ui_symbol_ok(symbol, "ui::combo_item_symbol_label") &&
           nk_combo_item_symbol_text(ctx, (enum nk_symbol_type)symbol, (const char *)text.data,
                                     ui_len(text, "ui::combo_item_symbol_label"), (nk_flags)align);
}

void gs_ui_combo_close(gs_ui_context c) {
    UI_CTX(c, "ui::combo_close", );
    if (ui_inside(u, UI_SCOPE_COMBO, "ui::combo_close")) nk_combo_close(ctx);
}

void gs_ui_combo_end(gs_ui_context c) {
    UI_CTX(c, "ui::combo_end", );
    if (!ui_in_window(u, "ui::combo_end") || !ui_expect_scope(u, UI_SCOPE_COMBO, "ui::combo_end"))
        return;
    nk_combo_end(ctx);
    ui_pop_scope(u);
}

/* --- contextual menus ------------------------------------------------------------------ */

uint8_t gs_ui_contextual_begin(gs_ui_context c, int64_t flags, gs_ui_float2 size,
                               gs_ui_rect trigger_bounds) {
    UI_CTX(c, "ui::contextual_begin", 0);
    if (!ui_popup_ok(u, "ui::contextual_begin") || !ui_flags_ok(flags, "ui::contextual_begin") ||
        !ui_vec_ok(size, "ui::contextual_begin") ||
        !ui_rect_ok(trigger_bounds, "ui::contextual_begin"))
        return 0;
    return ui_opened(u, nk_contextual_begin(ctx, (nk_flags)flags, ui_nk_vec2(size),
                                            ui_nk_rect(trigger_bounds)),
                     UI_SCOPE_CONTEXTUAL, "ui::contextual_begin");
}

uint8_t gs_ui_contextual_item_label(gs_ui_context c, gs_ui_bytes text, int64_t align) {
    UI_CTX(c, "ui::contextual_item_label", 0);
    return ui_inside(u, UI_SCOPE_CONTEXTUAL, "ui::contextual_item_label") &&
           nk_contextual_item_text(ctx, (const char *)text.data,
                                   ui_len(text, "ui::contextual_item_label"), (nk_flags)align);
}

uint8_t gs_ui_contextual_item_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                          int64_t align) {
    UI_CTX(c, "ui::contextual_item_image_label", 0);
    return ui_inside(u, UI_SCOPE_CONTEXTUAL, "ui::contextual_item_image_label") &&
           nk_contextual_item_image_text(ctx, ui_nk_image(img), (const char *)text.data,
                                         ui_len(text, "ui::contextual_item_image_label"),
                                         (nk_flags)align);
}

uint8_t gs_ui_contextual_item_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                           int64_t align) {
    UI_CTX(c, "ui::contextual_item_symbol_label", 0);
    return ui_inside(u, UI_SCOPE_CONTEXTUAL, "ui::contextual_item_symbol_label") &&
           ui_symbol_ok(symbol, "ui::contextual_item_symbol_label") &&
           nk_contextual_item_symbol_text(ctx, (enum nk_symbol_type)symbol,
                                          (const char *)text.data,
                                          ui_len(text, "ui::contextual_item_symbol_label"),
                                          (nk_flags)align);
}

void gs_ui_contextual_close(gs_ui_context c) {
    UI_CTX(c, "ui::contextual_close", );
    if (ui_inside(u, UI_SCOPE_CONTEXTUAL, "ui::contextual_close")) nk_contextual_close(ctx);
}

void gs_ui_contextual_end(gs_ui_context c) {
    UI_CTX(c, "ui::contextual_end", );
    if (!ui_in_window(u, "ui::contextual_end") ||
        !ui_expect_scope(u, UI_SCOPE_CONTEXTUAL, "ui::contextual_end"))
        return;
    nk_contextual_end(ctx);
    ui_pop_scope(u);
}

/* --- tooltips ------------------------------------------------------------------------- */

static bool ui_tooltip_pos_ok(int64_t position, gs_ui_float2 offset, const char *fn) {
    return ui_enum_ok(position, 9, fn, "tooltip position (TOP_LEFT ... BOTTOM_RIGHT)") &&
           ui_vec_ok(offset, fn);
}

void gs_ui_tooltip(gs_ui_context c, gs_ui_bytes text) {
    UI_CTX(c, "ui::tooltip", );
    if (ui_popup_ok(u, "ui::tooltip")) nk_tooltip(ctx, ui_cstr(text, 0));
}

void gs_ui_tooltip_offset(gs_ui_context c, gs_ui_bytes text, int64_t position,
                          gs_ui_float2 offset) {
    UI_CTX(c, "ui::tooltip_offset", );
    if (ui_popup_ok(u, "ui::tooltip_offset") &&
        ui_tooltip_pos_ok(position, offset, "ui::tooltip_offset"))
        nk_tooltip_offset(ctx, ui_cstr(text, 0), (enum nk_tooltip_pos)position, ui_nk_vec2(offset));
}

uint8_t gs_ui_tooltip_begin(gs_ui_context c, float width) {
    UI_CTX(c, "ui::tooltip_begin", 0);
    if (!ui_popup_ok(u, "ui::tooltip_begin") || !ui_float_ok(width, "ui::tooltip_begin", "a width of"))
        return 0;
    return ui_opened(u, nk_tooltip_begin(ctx, width), UI_SCOPE_TOOLTIP, "ui::tooltip_begin");
}

uint8_t gs_ui_tooltip_begin_offset(gs_ui_context c, float width, int64_t position,
                                   gs_ui_float2 offset) {
    UI_CTX(c, "ui::tooltip_begin_offset", 0);
    if (!ui_popup_ok(u, "ui::tooltip_begin_offset") ||
        !ui_float_ok(width, "ui::tooltip_begin_offset", "a width of") ||
        !ui_tooltip_pos_ok(position, offset, "ui::tooltip_begin_offset"))
        return 0;
    return ui_opened(u, nk_tooltip_begin_offset(ctx, width, (enum nk_tooltip_pos)position,
                                                ui_nk_vec2(offset)),
                     UI_SCOPE_TOOLTIP, "ui::tooltip_begin_offset");
}

void gs_ui_tooltip_end(gs_ui_context c) {
    UI_CTX(c, "ui::tooltip_end", );
    if (!ui_in_window(u, "ui::tooltip_end") ||
        !ui_expect_scope(u, UI_SCOPE_TOOLTIP, "ui::tooltip_end"))
        return;
    nk_tooltip_end(ctx);
    ui_pop_scope(u);
}

void gs_ui_do_tooltip(gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds) {
    UI_CTX(c, "ui::do_tooltip", );
    if (ui_popup_ok(u, "ui::do_tooltip") && ui_rect_ok(bounds, "ui::do_tooltip"))
        nk_do_tooltip(ctx, ui_cstr(text, 0), ui_nk_rect(bounds));
}

void gs_ui_do_tooltip_delay(gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds, float *timer) {
    UI_CTX(c, "ui::do_tooltip_delay", );
    if (ui_popup_ok(u, "ui::do_tooltip_delay") && ui_rect_ok(bounds, "ui::do_tooltip_delay") &&
        ui_float_ok(*timer, "ui::do_tooltip_delay", "a timer of"))
        nk_do_tooltip_delay(ctx, ui_cstr(text, 0), ui_nk_rect(bounds), timer);
}

void gs_ui_do_tooltip_delay_clicked(gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds,
                                    float *timer, uint8_t *clicked) {
    UI_CTX(c, "ui::do_tooltip_delay_clicked", );
    if (!ui_popup_ok(u, "ui::do_tooltip_delay_clicked") ||
        !ui_rect_ok(bounds, "ui::do_tooltip_delay_clicked") ||
        !ui_float_ok(*timer, "ui::do_tooltip_delay_clicked", "a timer of"))
        return;
    nk_bool k = *clicked != 0;
    nk_do_tooltip_delay_clicked(ctx, ui_cstr(text, 0), ui_nk_rect(bounds), timer, &k);
    *clicked = k != 0;
}

/* --- menus ------------------------------------------------------------------------------ */

void gs_ui_menubar_begin(gs_ui_context c) {
    UI_PANEL(c, "ui::menubar_begin", );
    const struct nk_panel *layout = ctx->current->layout;
    if (layout->at_y != layout->bounds.y) {
        ui_misuse("ui::menubar_begin: a menu bar is the first thing in its window or group, "
                  "before any row or widget");
        return;
    }
    if (ui_push_scope(u, UI_SCOPE_MENUBAR, true, "ui::menubar_begin")) nk_menubar_begin(ctx);
}

void gs_ui_menubar_end(gs_ui_context c) {
    UI_PANEL(c, "ui::menubar_end", );
    if (!ui_expect_scope(u, UI_SCOPE_MENUBAR, "ui::menubar_end")) return;
    nk_menubar_end(ctx);
    ui_pop_scope(u);
}

static bool ui_menu_ok(ui_ctx *u, gs_ui_float2 size, const char *fn) {
    return ui_popup_ok(u, fn) && ui_vec_ok(size, fn);
}

uint8_t gs_ui_menu_begin_label(gs_ui_context c, gs_ui_bytes text, int64_t align,
                               gs_ui_float2 size) {
    UI_CTX(c, "ui::menu_begin_label", 0);
    if (!ui_menu_ok(u, size, "ui::menu_begin_label")) return 0;
    return ui_opened(u, nk_menu_begin_text(ctx, (const char *)text.data,
                                           ui_len(text, "ui::menu_begin_label"), (nk_flags)align,
                                           ui_nk_vec2(size)),
                     UI_SCOPE_MENU, "ui::menu_begin_label");
}

uint8_t gs_ui_menu_begin_image(gs_ui_context c, gs_ui_bytes id, gs_ui_image img,
                               gs_ui_float2 size) {
    UI_CTX(c, "ui::menu_begin_image", 0);
    if (!ui_menu_ok(u, size, "ui::menu_begin_image")) return 0;
    return ui_opened(u, nk_menu_begin_image(ctx, ui_cstr(id, 0), ui_nk_image(img), ui_nk_vec2(size)),
                     UI_SCOPE_MENU, "ui::menu_begin_image");
}

uint8_t gs_ui_menu_begin_image_label(gs_ui_context c, gs_ui_bytes text, int64_t align,
                                     gs_ui_image img, gs_ui_float2 size) {
    UI_CTX(c, "ui::menu_begin_image_label", 0);
    if (!ui_menu_ok(u, size, "ui::menu_begin_image_label")) return 0;
    return ui_opened(u, nk_menu_begin_image_text(ctx, (const char *)text.data,
                                                 ui_len(text, "ui::menu_begin_image_label"),
                                                 (nk_flags)align, ui_nk_image(img),
                                                 ui_nk_vec2(size)),
                     UI_SCOPE_MENU, "ui::menu_begin_image_label");
}

uint8_t gs_ui_menu_begin_symbol(gs_ui_context c, gs_ui_bytes id, int64_t symbol,
                                gs_ui_float2 size) {
    UI_CTX(c, "ui::menu_begin_symbol", 0);
    if (!ui_menu_ok(u, size, "ui::menu_begin_symbol") ||
        !ui_symbol_ok(symbol, "ui::menu_begin_symbol"))
        return 0;
    return ui_opened(u, nk_menu_begin_symbol(ctx, ui_cstr(id, 0), (enum nk_symbol_type)symbol,
                                             ui_nk_vec2(size)),
                     UI_SCOPE_MENU, "ui::menu_begin_symbol");
}

uint8_t gs_ui_menu_begin_symbol_label(gs_ui_context c, gs_ui_bytes text, int64_t align,
                                      int64_t symbol, gs_ui_float2 size) {
    UI_CTX(c, "ui::menu_begin_symbol_label", 0);
    if (!ui_menu_ok(u, size, "ui::menu_begin_symbol_label") ||
        !ui_symbol_ok(symbol, "ui::menu_begin_symbol_label"))
        return 0;
    return ui_opened(u, nk_menu_begin_symbol_text(ctx, (const char *)text.data,
                                                  ui_len(text, "ui::menu_begin_symbol_label"),
                                                  (nk_flags)align, (enum nk_symbol_type)symbol,
                                                  ui_nk_vec2(size)),
                     UI_SCOPE_MENU, "ui::menu_begin_symbol_label");
}

uint8_t gs_ui_menu_item_label(gs_ui_context c, gs_ui_bytes text, int64_t align) {
    UI_CTX(c, "ui::menu_item_label", 0);
    return ui_inside(u, UI_SCOPE_MENU, "ui::menu_item_label") &&
           nk_menu_item_text(ctx, (const char *)text.data, ui_len(text, "ui::menu_item_label"),
                             (nk_flags)align);
}

uint8_t gs_ui_menu_item_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                    int64_t align) {
    UI_CTX(c, "ui::menu_item_image_label", 0);
    return ui_inside(u, UI_SCOPE_MENU, "ui::menu_item_image_label") &&
           nk_menu_item_image_text(ctx, ui_nk_image(img), (const char *)text.data,
                                   ui_len(text, "ui::menu_item_image_label"), (nk_flags)align);
}

uint8_t gs_ui_menu_item_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                     int64_t align) {
    UI_CTX(c, "ui::menu_item_symbol_label", 0);
    return ui_inside(u, UI_SCOPE_MENU, "ui::menu_item_symbol_label") &&
           ui_symbol_ok(symbol, "ui::menu_item_symbol_label") &&
           nk_menu_item_symbol_text(ctx, (enum nk_symbol_type)symbol, (const char *)text.data,
                                    ui_len(text, "ui::menu_item_symbol_label"), (nk_flags)align);
}

void gs_ui_menu_close(gs_ui_context c) {
    UI_CTX(c, "ui::menu_close", );
    if (ui_inside(u, UI_SCOPE_MENU, "ui::menu_close")) nk_menu_close(ctx);
}

void gs_ui_menu_end(gs_ui_context c) {
    UI_CTX(c, "ui::menu_end", );
    if (!ui_in_window(u, "ui::menu_end") || !ui_expect_scope(u, UI_SCOPE_MENU, "ui::menu_end"))
        return;
    nk_menu_end(ctx);
    ui_pop_scope(u);
}
