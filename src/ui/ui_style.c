/* Style: nk_style and the Goose-shaped gs_ui_style, converted field by
   field, and the style and color functions. */

#include "ui_internal.h"

#include <math.h>

/* --- conversions -----------------------------------------------------------------
   Each struct's fields are listed once, with what kind of value each is,
   and the lists expand into the conversions both ways and into the checks
   of what comes from Goose. The kinds: a style item, a color, a float, a
   color factor (0 to 1), a vector, flags, a bool, a symbol, the enums for
   underlines, header alignment and tooltip positions, and the nested
   styles. */

#define UI_TEXT_FIELDS(F) \
    F(color, color) F(vec, padding) F(fac, color_factor) F(fac, disabled_factor)
#define UI_LINK_FIELDS(F) \
    F(color, text_normal) F(color, text_hover) F(color, text_active) F(underline, underline) \
    F(f, underline_thickness) F(vec, padding) F(vec, touch_padding) F(fac, color_factor) \
    F(fac, disabled_factor)
#define UI_BUTTON_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) \
    F(fac, color_factor_background) F(color, text_background) F(color, text_normal) \
    F(color, text_hover) F(color, text_active) F(flags, text_alignment) \
    F(fac, color_factor_text) F(f, border) F(f, rounding) F(vec, padding) F(vec, image_padding) \
    F(vec, touch_padding) F(fac, disabled_factor)
#define UI_TOGGLE_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(item, cursor_normal) \
    F(item, cursor_hover) F(color, text_normal) F(color, text_hover) F(color, text_active) \
    F(color, text_background) F(flags, text_alignment) F(vec, padding) F(vec, touch_padding) \
    F(f, spacing) F(f, border) F(fac, color_factor) F(fac, disabled_factor)
#define UI_SELECTABLE_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, pressed) F(item, normal_active) \
    F(item, hover_active) F(item, pressed_active) F(color, text_normal) F(color, text_hover) \
    F(color, text_pressed) F(color, text_normal_active) F(color, text_hover_active) \
    F(color, text_pressed_active) F(color, text_background) F(flags, text_alignment) \
    F(f, rounding) F(vec, padding) F(vec, touch_padding) F(vec, image_padding) \
    F(fac, color_factor) F(fac, disabled_factor)
#define UI_SLIDER_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(color, bar_normal) \
    F(color, bar_hover) F(color, bar_active) F(color, bar_filled) F(item, cursor_normal) \
    F(item, cursor_hover) F(item, cursor_active) F(f, border) F(f, rounding) F(f, bar_height) \
    F(vec, padding) F(vec, spacing) F(vec, cursor_size) F(fac, color_factor) \
    F(fac, disabled_factor) F(b8, show_buttons) F(button, inc_button) F(button, dec_button) \
    F(sym, inc_symbol) F(sym, dec_symbol)
#define UI_KNOB_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(color, knob_normal) \
    F(color, knob_hover) F(color, knob_active) F(color, knob_border_color) \
    F(color, cursor_normal) F(color, cursor_hover) F(color, cursor_active) F(f, border) \
    F(f, knob_border) F(vec, padding) F(vec, spacing) F(f, cursor_width) F(fac, color_factor) \
    F(fac, disabled_factor)
#define UI_PROGRESS_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(item, cursor_normal) \
    F(item, cursor_hover) F(item, cursor_active) F(color, cursor_border_color) F(f, rounding) \
    F(f, border) F(f, cursor_border) F(f, cursor_rounding) F(vec, padding) \
    F(fac, color_factor) F(fac, disabled_factor)
#define UI_SCROLLBAR_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(item, cursor_normal) \
    F(item, cursor_hover) F(item, cursor_active) F(color, cursor_border_color) F(f, border) \
    F(f, rounding) F(f, border_cursor) F(f, rounding_cursor) F(vec, padding) \
    F(fac, color_factor) F(fac, disabled_factor) F(b8, show_buttons) F(button, inc_button) \
    F(button, dec_button) F(sym, inc_symbol) F(sym, dec_symbol)
#define UI_EDIT_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) \
    F(scrollbar, scrollbar) F(color, cursor_normal) F(color, cursor_hover) \
    F(color, cursor_text_normal) F(color, cursor_text_hover) F(color, text_normal) \
    F(color, text_hover) F(color, text_active) F(color, selected_normal) \
    F(color, selected_hover) F(color, selected_text_normal) F(color, selected_text_hover) \
    F(f, border) F(f, rounding) F(f, cursor_size) F(vec, scrollbar_size) F(vec, padding) \
    F(f, row_padding) F(fac, color_factor) F(fac, disabled_factor)
#define UI_PROPERTY_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(color, label_normal) \
    F(color, label_hover) F(color, label_active) F(sym, sym_left) F(sym, sym_right) \
    F(f, border) F(f, rounding) F(vec, padding) F(fac, color_factor) F(fac, disabled_factor) \
    F(edit, edit) F(button, inc_button) F(button, dec_button)
#define UI_CHART_FIELDS(F) \
    F(item, background) F(color, border_color) F(color, selected_color) F(color, color) \
    F(f, border) F(f, rounding) F(vec, padding) F(fac, color_factor) F(fac, disabled_factor) \
    F(b8, show_markers)
#define UI_COMBO_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(color, border_color) F(color, label_normal) \
    F(color, label_hover) F(color, label_active) F(color, symbol_normal) \
    F(color, symbol_hover) F(color, symbol_active) F(button, button) F(sym, sym_normal) \
    F(sym, sym_hover) F(sym, sym_active) F(f, border) F(f, rounding) F(vec, content_padding) \
    F(vec, button_padding) F(vec, spacing) F(fac, color_factor) F(fac, disabled_factor)
#define UI_TAB_FIELDS(F) \
    F(item, background) F(color, border_color) F(color, text) F(button, tab_maximize_button) \
    F(button, tab_minimize_button) F(button, node_maximize_button) \
    F(button, node_minimize_button) F(sym, sym_minimize) F(sym, sym_maximize) F(f, border) \
    F(f, rounding) F(f, indent) F(vec, padding) F(vec, spacing) F(fac, color_factor) \
    F(fac, disabled_factor)
#define UI_HEADER_FIELDS(F) \
    F(item, normal) F(item, hover) F(item, active) F(button, close_button) \
    F(button, minimize_button) F(sym, close_symbol) F(sym, minimize_symbol) \
    F(sym, maximize_symbol) F(color, label_normal) F(color, label_hover) F(color, label_active) \
    F(align, align) F(vec, padding) F(vec, label_padding) F(vec, spacing)
#define UI_WINDOW_FIELDS(F) \
    F(header, header) F(item, fixed_background) F(color, background) F(color, border_color) \
    F(color, popup_border_color) F(color, combo_border_color) \
    F(color, contextual_border_color) F(color, menu_border_color) \
    F(color, group_border_color) F(color, tooltip_border_color) F(item, scaler) F(f, border) \
    F(f, combo_border) F(f, contextual_border) F(f, menu_border) F(f, group_border) \
    F(f, tooltip_border) F(f, popup_border) F(f, min_row_height_padding) F(f, rounding) \
    F(vec, spacing) F(vec, scrollbar_size) F(vec, min_size) F(vec, padding) \
    F(vec, group_padding) F(vec, popup_padding) F(vec, combo_padding) \
    F(vec, contextual_padding) F(vec, menu_padding) F(vec, tooltip_padding) \
    F(tpos, tooltip_origin) F(vec, tooltip_offset) F(f, tooltip_delay)
#define UI_STYLE_FIELDS(F) \
    F(text, text) F(link, link) F(button, button) F(button, contextual_button) \
    F(button, menu_button) F(toggle, option) F(toggle, checkbox) F(selectable, selectable) \
    F(slider, slider) F(knob, knob) F(progress, progress) F(property, property) F(edit, edit) \
    F(chart, chart) F(scrollbar, scrollh) F(scrollbar, scrollv) F(tab, tab) F(combo, combo) \
    F(window, window)

static void ui_item_to_gs(const struct nk_style_item *n, gs_ui_style_item *g) {
    memset(g, 0, sizeof *g);
    g->kind = (int32_t)n->type;
    if (n->type == NK_STYLE_ITEM_COLOR) {
        g->color = ui_color_of(n->data.color);
    } else if (n->type == NK_STYLE_ITEM_IMAGE) {
        g->img = ui_image_of(n->data.image);
    } else {
        g->img = ui_image_of(n->data.slice.img);
        g->l = n->data.slice.l;
        g->t = n->data.slice.t;
        g->r = n->data.slice.r;
        g->b = n->data.slice.b;
    }
}

static void ui_item_to_nk(const gs_ui_style_item *g, struct nk_style_item *n) {
    memset(n, 0, sizeof *n);
    n->type = (enum nk_style_item_type)g->kind;
    if (g->kind == NK_STYLE_ITEM_COLOR) {
        n->data.color = ui_nk_color(g->color);
    } else if (g->kind == NK_STYLE_ITEM_IMAGE) {
        n->data.image = ui_nk_image(g->img);
    } else {
        n->data.slice.img = ui_nk_image(g->img);
        n->data.slice.l = g->l;
        n->data.slice.t = g->t;
        n->data.slice.r = g->r;
        n->data.slice.b = g->b;
    }
}

/* To Goose: nk_style_X -> gs_ui_style_X. */
#define UI_GS_item(n, g) ui_item_to_gs(&(n), &(g));
#define UI_GS_color(n, g) (g) = ui_color_of(n);
#define UI_GS_f(n, g) (g) = (n);
#define UI_GS_fac(n, g) (g) = (n);
#define UI_GS_vec(n, g) (g) = ui_float2_of(n);
#define UI_GS_flags(n, g) (g) = (uint32_t)(n);
#define UI_GS_b8(n, g) (g) = (n) != 0;
#define UI_GS_sym(n, g) (g) = (int32_t)(n);
#define UI_GS_underline(n, g) (g) = (int32_t)(n);
#define UI_GS_align(n, g) (g) = (int32_t)(n);
#define UI_GS_tpos(n, g) (g) = (int32_t)(n);
#define UI_GS_NESTED(kind, n, g) ui_##kind##_to_gs(&(n), &(g));
#define UI_GS_text(n, g) UI_GS_NESTED(text, n, g)
#define UI_GS_link(n, g) UI_GS_NESTED(link, n, g)
#define UI_GS_button(n, g) UI_GS_NESTED(button, n, g)
#define UI_GS_toggle(n, g) UI_GS_NESTED(toggle, n, g)
#define UI_GS_selectable(n, g) UI_GS_NESTED(selectable, n, g)
#define UI_GS_slider(n, g) UI_GS_NESTED(slider, n, g)
#define UI_GS_knob(n, g) UI_GS_NESTED(knob, n, g)
#define UI_GS_progress(n, g) UI_GS_NESTED(progress, n, g)
#define UI_GS_scrollbar(n, g) UI_GS_NESTED(scrollbar, n, g)
#define UI_GS_edit(n, g) UI_GS_NESTED(edit, n, g)
#define UI_GS_property(n, g) UI_GS_NESTED(property, n, g)
#define UI_GS_chart(n, g) UI_GS_NESTED(chart, n, g)
#define UI_GS_combo(n, g) UI_GS_NESTED(combo, n, g)
#define UI_GS_tab(n, g) UI_GS_NESTED(tab, n, g)
#define UI_GS_header(n, g) UI_GS_NESTED(header, n, g)
#define UI_GS_window(n, g) UI_GS_NESTED(window, n, g)

/* To Nuklear: gs_ui_style_X -> nk_style_X, the callbacks left as they are. */
#define UI_NK_item(g, n) ui_item_to_nk(&(g), &(n));
#define UI_NK_color(g, n) (n) = ui_nk_color(g);
#define UI_NK_f(g, n) (n) = (g);
#define UI_NK_fac(g, n) (n) = (g);
#define UI_NK_vec(g, n) (n) = ui_nk_vec2(g);
#define UI_NK_flags(g, n) (n) = (nk_flags)(g);
#define UI_NK_b8(g, n) (n) = (g) != 0;
#define UI_NK_sym(g, n) (n) = (enum nk_symbol_type)(g);
#define UI_NK_underline(g, n) (n) = (enum nk_link_underline)(g);
#define UI_NK_align(g, n) (n) = (enum nk_style_header_align)(g);
#define UI_NK_tpos(g, n) (n) = (enum nk_tooltip_pos)(g);
#define UI_NK_NESTED(kind, g, n) ui_##kind##_to_nk(&(g), &(n));
#define UI_NK_text(g, n) UI_NK_NESTED(text, g, n)
#define UI_NK_link(g, n) UI_NK_NESTED(link, g, n)
#define UI_NK_button(g, n) UI_NK_NESTED(button, g, n)
#define UI_NK_toggle(g, n) UI_NK_NESTED(toggle, g, n)
#define UI_NK_selectable(g, n) UI_NK_NESTED(selectable, g, n)
#define UI_NK_slider(g, n) UI_NK_NESTED(slider, g, n)
#define UI_NK_knob(g, n) UI_NK_NESTED(knob, g, n)
#define UI_NK_progress(g, n) UI_NK_NESTED(progress, g, n)
#define UI_NK_scrollbar(g, n) UI_NK_NESTED(scrollbar, g, n)
#define UI_NK_edit(g, n) UI_NK_NESTED(edit, g, n)
#define UI_NK_property(g, n) UI_NK_NESTED(property, g, n)
#define UI_NK_chart(g, n) UI_NK_NESTED(chart, g, n)
#define UI_NK_combo(g, n) UI_NK_NESTED(combo, g, n)
#define UI_NK_tab(g, n) UI_NK_NESTED(tab, g, n)
#define UI_NK_header(g, n) UI_NK_NESTED(header, g, n)
#define UI_NK_window(g, n) UI_NK_NESTED(window, g, n)

/* Checks of what comes from Goose: each is false, and a misuse naming the
   field, if the value would make Nuklear misbehave. */
static bool ui_check_field(bool ok, const char *fn, const char *field) {
    if (ok) return true;
    return ui_misuse("%s: the style's %s is not a value it can have", fn, field);
}
#define UI_CK_item(g, fn, name) \
    ui_check_field((g).kind >= NK_STYLE_ITEM_COLOR && (g).kind <= NK_STYLE_ITEM_NINE_SLICE, fn, name)
#define UI_CK_color(g, fn, name) true
#define UI_CK_f(g, fn, name) ui_check_field(isfinite(g), fn, name)
#define UI_CK_fac(g, fn, name) ui_check_field((g) >= 0 && (g) <= 1, fn, name)
#define UI_CK_vec(g, fn, name) ui_check_field(isfinite((g).x) && isfinite((g).y), fn, name)
#define UI_CK_flags(g, fn, name) true
#define UI_CK_b8(g, fn, name) true
#define UI_CK_sym(g, fn, name) ui_check_field((g) >= 0 && (g) < NK_SYMBOL_MAX, fn, name)
#define UI_CK_underline(g, fn, name) ui_check_field((g) >= 0 && (g) <= 2, fn, name)
#define UI_CK_align(g, fn, name) ui_check_field((g) >= 0 && (g) <= 1, fn, name)
#define UI_CK_tpos(g, fn, name) ui_check_field((g) >= 0 && (g) <= 8, fn, name)
#define UI_CK_NESTED(kind, g, fn) ui_##kind##_check(&(g), fn)
#define UI_CK_text(g, fn, name) UI_CK_NESTED(text, g, fn)
#define UI_CK_link(g, fn, name) UI_CK_NESTED(link, g, fn)
#define UI_CK_button(g, fn, name) UI_CK_NESTED(button, g, fn)
#define UI_CK_toggle(g, fn, name) UI_CK_NESTED(toggle, g, fn)
#define UI_CK_selectable(g, fn, name) UI_CK_NESTED(selectable, g, fn)
#define UI_CK_slider(g, fn, name) UI_CK_NESTED(slider, g, fn)
#define UI_CK_knob(g, fn, name) UI_CK_NESTED(knob, g, fn)
#define UI_CK_progress(g, fn, name) UI_CK_NESTED(progress, g, fn)
#define UI_CK_scrollbar(g, fn, name) UI_CK_NESTED(scrollbar, g, fn)
#define UI_CK_edit(g, fn, name) UI_CK_NESTED(edit, g, fn)
#define UI_CK_property(g, fn, name) UI_CK_NESTED(property, g, fn)
#define UI_CK_chart(g, fn, name) UI_CK_NESTED(chart, g, fn)
#define UI_CK_combo(g, fn, name) UI_CK_NESTED(combo, g, fn)
#define UI_CK_tab(g, fn, name) UI_CK_NESTED(tab, g, fn)
#define UI_CK_header(g, fn, name) UI_CK_NESTED(header, g, fn)
#define UI_CK_window(g, fn, name) UI_CK_NESTED(window, g, fn)

#define UI_TO_GS(kind, name) UI_GS_##kind(n->name, g->name)
#define UI_TO_NK(kind, name) UI_NK_##kind(g->name, n->name)
#define UI_CHECK(kind, name) && UI_CK_##kind(g->name, fn, #name)

/* The three functions of one style struct. */
#define UI_STYLE_CONVERSIONS(kind, nktype, gstype, FIELDS) \
    static void ui_##kind##_to_gs(const struct nktype *n, gstype *g); \
    static void ui_##kind##_to_nk(const gstype *g, struct nktype *n); \
    static bool ui_##kind##_check(const gstype *g, const char *fn);

UI_STYLE_CONVERSIONS(text, nk_style_text, gs_ui_style_text, UI_TEXT_FIELDS)
UI_STYLE_CONVERSIONS(link, nk_style_link, gs_ui_style_link, UI_LINK_FIELDS)
UI_STYLE_CONVERSIONS(button, nk_style_button, gs_ui_style_button, UI_BUTTON_FIELDS)
UI_STYLE_CONVERSIONS(toggle, nk_style_toggle, gs_ui_style_toggle, UI_TOGGLE_FIELDS)
UI_STYLE_CONVERSIONS(selectable, nk_style_selectable, gs_ui_style_selectable, UI_SELECTABLE_FIELDS)
UI_STYLE_CONVERSIONS(slider, nk_style_slider, gs_ui_style_slider, UI_SLIDER_FIELDS)
UI_STYLE_CONVERSIONS(knob, nk_style_knob, gs_ui_style_knob, UI_KNOB_FIELDS)
UI_STYLE_CONVERSIONS(progress, nk_style_progress, gs_ui_style_progress, UI_PROGRESS_FIELDS)
UI_STYLE_CONVERSIONS(scrollbar, nk_style_scrollbar, gs_ui_style_scrollbar, UI_SCROLLBAR_FIELDS)
UI_STYLE_CONVERSIONS(edit, nk_style_edit, gs_ui_style_edit, UI_EDIT_FIELDS)
UI_STYLE_CONVERSIONS(property, nk_style_property, gs_ui_style_property, UI_PROPERTY_FIELDS)
UI_STYLE_CONVERSIONS(chart, nk_style_chart, gs_ui_style_chart, UI_CHART_FIELDS)
UI_STYLE_CONVERSIONS(combo, nk_style_combo, gs_ui_style_combo, UI_COMBO_FIELDS)
UI_STYLE_CONVERSIONS(tab, nk_style_tab, gs_ui_style_tab, UI_TAB_FIELDS)
UI_STYLE_CONVERSIONS(header, nk_style_window_header, gs_ui_style_window_header, UI_HEADER_FIELDS)
UI_STYLE_CONVERSIONS(window, nk_style_window, gs_ui_style_window, UI_WINDOW_FIELDS)
UI_STYLE_CONVERSIONS(all, nk_style, gs_ui_style, UI_STYLE_FIELDS)

#undef UI_STYLE_CONVERSIONS
#define UI_STYLE_CONVERSIONS(kind, nktype, gstype, FIELDS) \
    static void ui_##kind##_to_gs(const struct nktype *n, gstype *g) { \
        FIELDS(UI_TO_GS) \
    } \
    static void ui_##kind##_to_nk(const gstype *g, struct nktype *n) { \
        FIELDS(UI_TO_NK) \
    } \
    static bool ui_##kind##_check(const gstype *g, const char *fn) { \
        return true FIELDS(UI_CHECK); \
    }

UI_STYLE_CONVERSIONS(text, nk_style_text, gs_ui_style_text, UI_TEXT_FIELDS)
UI_STYLE_CONVERSIONS(link, nk_style_link, gs_ui_style_link, UI_LINK_FIELDS)
UI_STYLE_CONVERSIONS(button, nk_style_button, gs_ui_style_button, UI_BUTTON_FIELDS)
UI_STYLE_CONVERSIONS(toggle, nk_style_toggle, gs_ui_style_toggle, UI_TOGGLE_FIELDS)
UI_STYLE_CONVERSIONS(selectable, nk_style_selectable, gs_ui_style_selectable, UI_SELECTABLE_FIELDS)
UI_STYLE_CONVERSIONS(slider, nk_style_slider, gs_ui_style_slider, UI_SLIDER_FIELDS)
UI_STYLE_CONVERSIONS(knob, nk_style_knob, gs_ui_style_knob, UI_KNOB_FIELDS)
UI_STYLE_CONVERSIONS(progress, nk_style_progress, gs_ui_style_progress, UI_PROGRESS_FIELDS)
UI_STYLE_CONVERSIONS(scrollbar, nk_style_scrollbar, gs_ui_style_scrollbar, UI_SCROLLBAR_FIELDS)
UI_STYLE_CONVERSIONS(edit, nk_style_edit, gs_ui_style_edit, UI_EDIT_FIELDS)
UI_STYLE_CONVERSIONS(property, nk_style_property, gs_ui_style_property, UI_PROPERTY_FIELDS)
UI_STYLE_CONVERSIONS(chart, nk_style_chart, gs_ui_style_chart, UI_CHART_FIELDS)
UI_STYLE_CONVERSIONS(combo, nk_style_combo, gs_ui_style_combo, UI_COMBO_FIELDS)
UI_STYLE_CONVERSIONS(tab, nk_style_tab, gs_ui_style_tab, UI_TAB_FIELDS)
UI_STYLE_CONVERSIONS(header, nk_style_window_header, gs_ui_style_window_header, UI_HEADER_FIELDS)
UI_STYLE_CONVERSIONS(window, nk_style_window, gs_ui_style_window, UI_WINDOW_FIELDS)
UI_STYLE_CONVERSIONS(all, nk_style, gs_ui_style, UI_STYLE_FIELDS)

void ui_style_to_gs(const struct nk_style *n, gs_ui_style *g) {
    memset(g, 0, sizeof *g);
    ui_all_to_gs(n, g);
}

bool ui_style_check(const gs_ui_style *g, const char *fn) { return ui_all_check(g, fn); }

void ui_style_to_nk(const gs_ui_style *g, struct nk_style *n) { ui_all_to_nk(g, n); }

void ui_style_button_to_nk(const gs_ui_style_button *g, struct nk_style_button *n) {
    ui_button_to_nk(g, n);
}

bool ui_style_button_check(const gs_ui_style_button *g, const char *fn) {
    return ui_button_check(g, fn);
}

void ui_style_link_to_nk(const gs_ui_style_link *g, struct nk_style_link *n) {
    ui_link_to_nk(g, n);
}

bool ui_style_link_check(const gs_ui_style_link *g, const char *fn) {
    return ui_link_check(g, fn);
}

/* --- style ----------------------------------------------------------------------------- */

gs_ui_style gs_ui_style_of(gs_ui_context c) {
    gs_ui_style g;
    memset(&g, 0, sizeof g);
    UI_CTX(c, "ui::style", g);
    ui_style_to_gs(&ctx->style, &g);
    return g;
}

void gs_ui_set_style(gs_ui_context c, const gs_ui_style *style) {
    UI_CTX(c, "ui::set_style", );
    if (ui_style_check(style, "ui::set_style")) ui_style_to_nk(style, &ctx->style);
}

uint8_t gs_ui_push_style(gs_ui_context c, const gs_ui_style *style) {
    UI_CTX(c, "ui::push_style", 0);
    if (!ui_style_check(style, "ui::push_style")) return 0;
    if (u->nstyles == u->styles_cap) {
        u->styles_cap = u->styles_cap ? u->styles_cap * 2 : 4;
        u->styles = (gs_ui_style *)realloc(u->styles, sizeof(gs_ui_style) * (size_t)u->styles_cap);
    }
    ui_style_to_gs(&ctx->style, &u->styles[u->nstyles++]);
    ui_style_to_nk(style, &ctx->style);
    return 1;
}

uint8_t gs_ui_pop_style(gs_ui_context c) {
    UI_CTX(c, "ui::pop_style", 0);
    if (!u->nstyles) return ui_misuse("ui::pop_style with no style pushed");
    ui_style_to_nk(&u->styles[--u->nstyles], &ctx->style);
    return 1;
}

void gs_ui_style_default(gs_ui_context c) {
    UI_CTX(c, "ui::style_default", );
    nk_style_default(ctx);
}

void gs_ui_style_from_table(gs_ui_context c, gs_ui_color_slice table) {
    UI_CTX(c, "ui::style_from_table", );
    if (table.len != NK_COLOR_COUNT) {
        ui_misuse("ui::style_from_table: a table of %lld colors, where it has one per "
                  "COLOR_* (%d)", (long long)table.len, NK_COLOR_COUNT);
        return;
    }
    struct nk_color colors[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; i++) colors[i] = ui_nk_color(table.data[i]);
    nk_style_from_table(ctx, colors);
}

int64_t gs_ui_style_get_color_by_name(int64_t color, gs_ui_bytes out) {
    if (!ui_enum_ok(color, NK_COLOR_COUNT, "ui::style_get_color_by_name", "COLOR_*")) return 0;
    const char *name = nk_style_get_color_by_name((enum nk_style_colors)color);
    return ui_copy_out(name, (int64_t)strlen(name), out);
}

/* A font the style can use: from a baked atlas. */
static const struct nk_user_font *ui_style_font(gs_ui_font h, const char *fn) {
    ui_font *f = ui_font_get(h, fn);
    if (!f) return NULL;
    ui_atlas *a = (ui_atlas *)ui_table_find(&ui_atlases, f->atlas);
    if (!a->baked) {
        ui_misuse("%s: the font's atlas is not baked yet", fn);
        return NULL;
    }
    return &f->font->handle;
}

void gs_ui_style_set_font(gs_ui_context c, gs_ui_font font) {
    UI_CTX(c, "ui::style_set_font", );
    const struct nk_user_font *f = ui_style_font(font, "ui::style_set_font");
    if (f) nk_style_set_font(ctx, f);
}

uint8_t gs_ui_style_push_font(gs_ui_context c, gs_ui_font font) {
    UI_CTX(c, "ui::style_push_font", 0);
    const struct nk_user_font *f = ui_style_font(font, "ui::style_push_font");
    if (!f) return 0;
    if (ctx->stacks.fonts.head >= NK_FONT_STACK_SIZE)
        return ui_misuse("ui::style_push_font: %d fonts pushed and not popped", NK_FONT_STACK_SIZE);
    return nk_style_push_font(ctx, f) != 0;
}

uint8_t gs_ui_style_pop_font(gs_ui_context c) {
    UI_CTX(c, "ui::style_pop_font", 0);
    if (ctx->stacks.fonts.head < 1) return ui_misuse("ui::style_pop_font with no font pushed");
    return nk_style_pop_font(ctx) != 0;
}

static struct nk_cursor ui_nk_cursor(gs_ui_cursor g) {
    struct nk_cursor n;
    memset(&n, 0, sizeof n);
    n.img = ui_nk_image(g.img);
    n.size = ui_nk_vec2(g.size);
    n.offset = ui_nk_vec2(g.offset);
    return n;
}

static bool ui_cursor_ok(gs_ui_cursor g, const char *fn) {
    if (isfinite(g.size.x) && isfinite(g.size.y) && isfinite(g.offset.x) && isfinite(g.offset.y))
        return true;
    return ui_misuse("%s: a cursor %g x %g with its hot spot at %g, %g", fn, (double)g.size.x,
                     (double)g.size.y, (double)g.offset.x, (double)g.offset.y);
}

void gs_ui_style_load_cursor(gs_ui_context c, int64_t cursor, gs_ui_cursor image) {
    UI_CTX(c, "ui::style_load_cursor", );
    if (!ui_enum_ok(cursor, NK_CURSOR_COUNT, "ui::style_load_cursor", "CURSOR_*") ||
        !ui_cursor_ok(image, "ui::style_load_cursor"))
        return;
    /* Nuklear points at the cursor: the context keeps it. */
    u->cursors[cursor] = ui_nk_cursor(image);
    nk_style_load_cursor(ctx, (enum nk_style_cursor)cursor, &u->cursors[cursor]);
}

void gs_ui_style_load_all_cursors(gs_ui_context c, gs_ui_cursor_slice cursors) {
    UI_CTX(c, "ui::style_load_all_cursors", );
    if (cursors.len != NK_CURSOR_COUNT) {
        ui_misuse("ui::style_load_all_cursors: %lld cursors, where there is one per CURSOR_* (%d)",
                  (long long)cursors.len, NK_CURSOR_COUNT);
        return;
    }
    for (int i = 0; i < NK_CURSOR_COUNT; i++)
        if (!ui_cursor_ok(cursors.data[i], "ui::style_load_all_cursors")) return;
    for (int i = 0; i < NK_CURSOR_COUNT; i++) u->cursors[i] = ui_nk_cursor(cursors.data[i]);
    nk_style_load_all_cursors(ctx, u->cursors);
}

uint8_t gs_ui_style_set_cursor(gs_ui_context c, int64_t cursor) {
    UI_CTX(c, "ui::style_set_cursor", 0);
    return ui_enum_ok(cursor, NK_CURSOR_COUNT, "ui::style_set_cursor", "CURSOR_*") &&
           nk_style_set_cursor(ctx, (enum nk_style_cursor)cursor);
}

void gs_ui_style_show_cursor(gs_ui_context c) {
    UI_CTX(c, "ui::style_show_cursor", );
    nk_style_show_cursor(ctx);
}

void gs_ui_style_hide_cursor(gs_ui_context c) {
    UI_CTX(c, "ui::style_hide_cursor", );
    nk_style_hide_cursor(ctx);
}

/* --- colors ----------------------------------------------------------------------------- */

gs_ui_color gs_ui_rgb(int64_t r, int64_t g, int64_t b) {
    return ui_color_of(nk_rgb(ui_int(r), ui_int(g), ui_int(b)));
}

gs_ui_color gs_ui_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    return ui_color_of(nk_rgba(ui_int(r), ui_int(g), ui_int(b), ui_int(a)));
}

gs_ui_color gs_ui_rgba_u32(uint32_t rgba) { return ui_color_of(nk_rgba_u32(rgba)); }

gs_ui_color gs_ui_rgb_f(float r, float g, float b) { return ui_color_of(nk_rgb_f(r, g, b)); }

gs_ui_color gs_ui_rgba_f(float r, float g, float b, float a) {
    return ui_color_of(nk_rgba_f(r, g, b, a));
}

gs_ui_color gs_ui_rgb_cf(gs_ui_colorf c) { return ui_color_of(nk_rgb_cf(ui_nk_colorf(c))); }

gs_ui_color gs_ui_rgba_cf(gs_ui_colorf c) { return ui_color_of(nk_rgba_cf(ui_nk_colorf(c))); }

/* "#RRGGBB" or "RRGGBB", and eight digits with alpha: Nuklear reads that
   many characters whatever the string holds. */
static bool ui_hex_ok(gs_ui_bytes text, int digits, const char *fn, char *out) {
    int64_t skip = text.len > 0 && text.data[0] == '#';
    bool ok = text.len - skip == digits;
    for (int64_t i = skip; ok && i < text.len; i++) {
        uint8_t ch = text.data[i];
        ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    }
    if (!ok)
        return ui_misuse("%s: \"%.*s\" is not %d hexadecimal digits, after an optional #", fn,
                         (int)(text.len < 64 ? text.len : 64), (const char *)text.data, digits);
    memcpy(out, text.data + skip, (size_t)digits);
    out[digits] = 0;
    return true;
}

gs_ui_color gs_ui_rgb_hex(gs_ui_bytes text) {
    char hex[16];
    gs_ui_color black = { 0, 0, 0, 255 };
    return ui_hex_ok(text, 6, "ui::rgb_hex", hex) ? ui_color_of(nk_rgb_hex(hex)) : black;
}

gs_ui_color gs_ui_rgba_hex(gs_ui_bytes text) {
    char hex[16];
    gs_ui_color black = { 0, 0, 0, 255 };
    return ui_hex_ok(text, 8, "ui::rgba_hex", hex) ? ui_color_of(nk_rgba_hex(hex)) : black;
}

gs_ui_color gs_ui_rgb_factor(gs_ui_color c, float factor) {
    if (!(factor >= 0 && factor <= 1)) {
        ui_misuse("ui::rgb_factor: a factor of %g (0 to 1)", (double)factor);
        return c;
    }
    return ui_color_of(nk_rgb_factor(ui_nk_color(c), factor));
}

gs_ui_color gs_ui_hsv(int64_t h, int64_t s, int64_t v) {
    return ui_color_of(nk_hsv(ui_int(h), ui_int(s), ui_int(v)));
}

gs_ui_color gs_ui_hsva(int64_t h, int64_t s, int64_t v, int64_t a) {
    return ui_color_of(nk_hsva(ui_int(h), ui_int(s), ui_int(v), ui_int(a)));
}

gs_ui_color gs_ui_hsv_f(float h, float s, float v) { return ui_color_of(nk_hsv_f(h, s, v)); }

gs_ui_color gs_ui_hsva_f(float h, float s, float v, float a) {
    return ui_color_of(nk_hsva_f(h, s, v, a));
}

gs_ui_colorf gs_ui_hsva_colorf(float h, float s, float v, float a) {
    return ui_colorf_of(nk_hsva_colorf(h, s, v, a));
}

gs_ui_float4 gs_ui_colorf_hsva_f(gs_ui_colorf c) {
    gs_ui_float4 r;
    nk_colorf_hsva_f(&r.x, &r.y, &r.z, &r.w, ui_nk_colorf(c));
    return r;
}

gs_ui_colorf gs_ui_color_cf(gs_ui_color c) { return ui_colorf_of(nk_color_cf(ui_nk_color(c))); }

uint32_t gs_ui_color_u32(gs_ui_color c) { return nk_color_u32(ui_nk_color(c)); }

int64_t gs_ui_color_hex_rgba(gs_ui_color c, gs_ui_bytes out) {
    char hex[16];
    nk_color_hex_rgba(hex, ui_nk_color(c));
    return ui_copy_out(hex, 8, out);
}

int64_t gs_ui_color_hex_rgb(gs_ui_color c, gs_ui_bytes out) {
    char hex[16];
    nk_color_hex_rgb(hex, ui_nk_color(c));
    return ui_copy_out(hex, 6, out);
}

gs_ui_float3 gs_ui_color_hsv_f(gs_ui_color c) {
    gs_ui_float3 r;
    nk_color_hsv_f(&r.x, &r.y, &r.z, ui_nk_color(c));
    return r;
}

gs_ui_float4 gs_ui_color_hsva_f(gs_ui_color c) {
    gs_ui_float4 r;
    nk_color_hsva_f(&r.x, &r.y, &r.z, &r.w, ui_nk_color(c));
    return r;
}
