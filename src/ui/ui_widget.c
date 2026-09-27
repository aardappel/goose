/* Widgets: text, images, links, buttons, toggles, selectables, sliders,
   knobs, progress bars, color pickers, properties, text editing and charts,
   and the queries about the next widget's place. */

#include "ui_internal.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>

/* A widget takes the next place in the current row, so there has to be one:
   Nuklear divides by a row's columns. */
static bool ui_placed(ui_ctx *u, const char *fn) {
    if (!ui_in_panel(u, fn)) return false;
    if (u->nk.current->layout->row.columns > 0) return true;
    return ui_misuse("%s before any row: a ui::layout_row_* call lays out where widgets go", fn);
}

#define UI_WIDGET(c, fn, fail) \
    UI_CTX(c, fn, fail); \
    if (!ui_placed(u, fn)) return fail

static const char *ui_text(gs_ui_bytes s) {
    return (const char *)s.data;
}

static bool ui_finite(float f, const char *fn, const char *what) {
    if (isfinite(f)) return true;
    return ui_misuse("%s: %s %g", fn, what, (double)f);
}

/* A slider, knob or property's range and step: finite, and a step that
   moves. */
static bool ui_range_ok(double min, double max, double step, const char *fn) {
    if (isfinite(min) && isfinite(max) && isfinite(step) && step > 0) return true;
    return ui_misuse("%s: a range from %g to %g in steps of %g", fn, min, max, step);
}

/* Sliders and knobs divide by their range. */
static bool ui_span_ok(double min, double max, double step, const char *fn) {
    if (min == max)
        return ui_misuse("%s: values from %g to %g, where the range must not be empty", fn, min,
                         max);
    return ui_range_ok(min, max, step, fn);
}

/* --- widgets in general ---------------------------------------------------------- */

int64_t gs_ui_widget(gs_ui_context c, gs_ui_rect *bounds) {
    memset(bounds, 0, sizeof *bounds);
    UI_WIDGET(c, "ui::widget", NK_WIDGET_INVALID);
    struct nk_rect r;
    enum nk_widget_layout_states s = nk_widget(&r, ctx);
    *bounds = ui_rect_of(r);
    return s;
}

gs_ui_rect gs_ui_widget_bounds(gs_ui_context c) {
    gs_ui_rect none = { 0, 0, 0, 0 };
    UI_WIDGET(c, "ui::widget_bounds", none);
    return ui_rect_of(nk_widget_bounds(ctx));
}

gs_ui_float2 gs_ui_widget_position(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_WIDGET(c, "ui::widget_position", none);
    return ui_float2_of(nk_widget_position(ctx));
}

gs_ui_float2 gs_ui_widget_size(gs_ui_context c) {
    gs_ui_float2 none = { 0, 0 };
    UI_WIDGET(c, "ui::widget_size", none);
    return ui_float2_of(nk_widget_size(ctx));
}

float gs_ui_widget_width(gs_ui_context c) {
    UI_WIDGET(c, "ui::widget_width", 0);
    return nk_widget_width(ctx);
}

float gs_ui_widget_height(gs_ui_context c) {
    UI_WIDGET(c, "ui::widget_height", 0);
    return nk_widget_height(ctx);
}

uint8_t gs_ui_widget_is_hovered(gs_ui_context c) {
    UI_WIDGET(c, "ui::widget_is_hovered", 0);
    return nk_widget_is_hovered(ctx) != 0;
}

uint8_t gs_ui_widget_is_mouse_clicked(gs_ui_context c, int64_t button) {
    UI_WIDGET(c, "ui::widget_is_mouse_clicked", 0);
    return ui_button_ok(button, "ui::widget_is_mouse_clicked") &&
           nk_widget_is_mouse_clicked(ctx, (enum nk_buttons)button);
}

uint8_t gs_ui_widget_has_mouse_click_down(gs_ui_context c, int64_t button, uint8_t down) {
    UI_WIDGET(c, "ui::widget_has_mouse_click_down", 0);
    return ui_button_ok(button, "ui::widget_has_mouse_click_down") &&
           nk_widget_has_mouse_click_down(ctx, (enum nk_buttons)button, down != 0);
}

void gs_ui_spacing(gs_ui_context c, int64_t cols) {
    UI_WIDGET(c, "ui::spacing", );
    if (cols < 0 || cols > 1024) {
        ui_misuse("ui::spacing: %lld columns", (long long)cols);
        return;
    }
    nk_spacing(ctx, (int)cols);
}

void gs_ui_widget_disable_begin(gs_ui_context c) {
    UI_PANEL(c, "ui::widget_disable_begin", );
    if (ui_push_scope(u, UI_SCOPE_DISABLED, true, "ui::widget_disable_begin"))
        nk_widget_disable_begin(ctx);
}

void gs_ui_widget_disable_end(gs_ui_context c) {
    UI_PANEL(c, "ui::widget_disable_end", );
    if (!ui_expect_scope(u, UI_SCOPE_DISABLED, "ui::widget_disable_end")) return;
    nk_widget_disable_end(ctx);
    ui_pop_scope(u);
}

/* --- text and images ------------------------------------------------------------- */

void gs_ui_label(gs_ui_context c, gs_ui_bytes text, int64_t align) {
    UI_WIDGET(c, "ui::label", );
    nk_text(ctx, ui_text(text), ui_len(text, "ui::label"), (nk_flags)align);
}

void gs_ui_label_colored(gs_ui_context c, gs_ui_bytes text, int64_t align, gs_ui_color color) {
    UI_WIDGET(c, "ui::label_colored", );
    nk_text_colored(ctx, ui_text(text), ui_len(text, "ui::label_colored"), (nk_flags)align,
                    ui_nk_color(color));
}

void gs_ui_label_wrap(gs_ui_context c, gs_ui_bytes text) {
    UI_WIDGET(c, "ui::label_wrap", );
    nk_text_wrap(ctx, ui_text(text), ui_len(text, "ui::label_wrap"));
}

void gs_ui_label_colored_wrap(gs_ui_context c, gs_ui_bytes text, gs_ui_color color) {
    UI_WIDGET(c, "ui::label_colored_wrap", );
    nk_text_wrap_colored(ctx, ui_text(text), ui_len(text, "ui::label_colored_wrap"),
                         ui_nk_color(color));
}

void gs_ui_image_widget(gs_ui_context c, gs_ui_image img) {
    UI_WIDGET(c, "ui::image", );
    nk_image(ctx, ui_nk_image(img));
}

void gs_ui_image_color(gs_ui_context c, gs_ui_image img, gs_ui_color color) {
    UI_WIDGET(c, "ui::image_color", );
    nk_image_color(ctx, ui_nk_image(img), ui_nk_color(color));
}

/* Nuklear's nk_value_* format with printf, which it has only with varargs
   compiled in: the same formats, here. */
static void ui_value(ui_ctx *u, const char *fmt, gs_ui_bytes prefix, ...) {
    char buf[256];
    va_list args;
    va_start(args, prefix);
    char pre[128];
    snprintf(pre, sizeof pre, "%.*s", (int)(prefix.len < 100 ? prefix.len : 100),
             (const char *)prefix.data);
    int n = snprintf(buf, sizeof buf, "%s: ", pre);
    vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, args);
    va_end(args);
    nk_text(&u->nk, buf, (int)strlen(buf), NK_TEXT_LEFT);
}

void gs_ui_value_bool(gs_ui_context c, gs_ui_bytes prefix, uint8_t value) {
    UI_WIDGET(c, "ui::value_bool", );
    ui_value(u, "%s", prefix, value ? "true" : "false");
}

void gs_ui_value_int(gs_ui_context c, gs_ui_bytes prefix, int64_t value) {
    UI_WIDGET(c, "ui::value_int", );
    ui_value(u, "%lld", prefix, (long long)value);
}

void gs_ui_value_uint(gs_ui_context c, gs_ui_bytes prefix, uint64_t value) {
    UI_WIDGET(c, "ui::value_uint", );
    ui_value(u, "%llu", prefix, (unsigned long long)value);
}

void gs_ui_value_float(gs_ui_context c, gs_ui_bytes prefix, float value) {
    UI_WIDGET(c, "ui::value_float", );
    ui_value(u, "%.3f", prefix, (double)value);
}

void gs_ui_value_color_byte(gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color) {
    UI_WIDGET(c, "ui::value_color_byte", );
    ui_value(u, "(%d, %d, %d, %d)", prefix, color.r, color.g, color.b, color.a);
}

void gs_ui_value_color_float(gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color) {
    UI_WIDGET(c, "ui::value_color_float", );
    double v[4];
    nk_color_dv(v, ui_nk_color(color));
    ui_value(u, "(%.2f, %.2f, %.2f, %.2f)", prefix, v[0], v[1], v[2], v[3]);
}

void gs_ui_value_color_hex(gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color) {
    UI_WIDGET(c, "ui::value_color_hex", );
    char hex[16];
    nk_color_hex_rgba(hex, ui_nk_color(color));
    ui_value(u, "%s", prefix, hex);
}

static bool ui_underline_ok(int64_t underline, const char *fn) {
    return ui_enum_ok(underline, 3, fn, "LINK_UNDERLINE_*");
}

uint8_t gs_ui_link_label(gs_ui_context c, gs_ui_bytes text, int64_t align) {
    UI_WIDGET(c, "ui::link_label", 0);
    return nk_link_text(ctx, ui_text(text), ui_len(text, "ui::link_label"), (nk_flags)align) != 0;
}

uint8_t gs_ui_link_label_colored(gs_ui_context c, gs_ui_bytes text, int64_t align,
                                 gs_ui_color color) {
    UI_WIDGET(c, "ui::link_label_colored", 0);
    return nk_link_text_colored(ctx, ui_text(text), ui_len(text, "ui::link_label_colored"),
                                (nk_flags)align, ui_nk_color(color)) != 0;
}

uint8_t gs_ui_link_label_underline(gs_ui_context c, gs_ui_bytes text, int64_t align,
                                   int64_t underline) {
    UI_WIDGET(c, "ui::link_label_underline", 0);
    return ui_underline_ok(underline, "ui::link_label_underline") &&
           nk_link_text_underline(ctx, ui_text(text), ui_len(text, "ui::link_label_underline"),
                                  (nk_flags)align, (enum nk_link_underline)underline);
}

uint8_t gs_ui_link_label_styled(gs_ui_context c, const gs_ui_style_link *style, gs_ui_bytes text,
                                int64_t align) {
    UI_WIDGET(c, "ui::link_label_styled", 0);
    if (!ui_style_link_check(style, "ui::link_label_styled")) return 0;
    struct nk_style_link s = ctx->style.link;
    ui_style_link_to_nk(style, &s);
    return nk_link_text_styled(ctx, &s, ui_text(text), ui_len(text, "ui::link_label_styled"),
                               (nk_flags)align) != 0;
}

/* --- buttons ------------------------------------------------------------------------ */

uint8_t gs_ui_button_label(gs_ui_context c, gs_ui_bytes text) {
    UI_WIDGET(c, "ui::button_label", 0);
    return nk_button_text(ctx, ui_text(text), ui_len(text, "ui::button_label")) != 0;
}

uint8_t gs_ui_button_color(gs_ui_context c, gs_ui_color color) {
    UI_WIDGET(c, "ui::button_color", 0);
    return nk_button_color(ctx, ui_nk_color(color)) != 0;
}

uint8_t gs_ui_button_symbol(gs_ui_context c, int64_t symbol) {
    UI_WIDGET(c, "ui::button_symbol", 0);
    return ui_symbol_ok(symbol, "ui::button_symbol") &&
           nk_button_symbol(ctx, (enum nk_symbol_type)symbol);
}

uint8_t gs_ui_button_image(gs_ui_context c, gs_ui_image img) {
    UI_WIDGET(c, "ui::button_image", 0);
    return nk_button_image(ctx, ui_nk_image(img)) != 0;
}

uint8_t gs_ui_button_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                  int64_t align) {
    UI_WIDGET(c, "ui::button_symbol_label", 0);
    return ui_symbol_ok(symbol, "ui::button_symbol_label") &&
           nk_button_symbol_text(ctx, (enum nk_symbol_type)symbol, ui_text(text),
                                 ui_len(text, "ui::button_symbol_label"), (nk_flags)align);
}

uint8_t gs_ui_button_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                 int64_t align) {
    UI_WIDGET(c, "ui::button_image_label", 0);
    return nk_button_image_text(ctx, ui_nk_image(img), ui_text(text),
                                ui_len(text, "ui::button_image_label"), (nk_flags)align) != 0;
}

/* A button style from Goose, on the context's own for the callbacks it has
   none of. */
static bool ui_button_style(ui_ctx *u, const gs_ui_style_button *g, struct nk_style_button *n,
                            const char *fn) {
    if (!ui_style_button_check(g, fn)) return false;
    *n = u->nk.style.button;
    ui_style_button_to_nk(g, n);
    return true;
}

uint8_t gs_ui_button_label_styled(gs_ui_context c, const gs_ui_style_button *style,
                                  gs_ui_bytes text) {
    UI_WIDGET(c, "ui::button_label_styled", 0);
    struct nk_style_button s;
    return ui_button_style(u, style, &s, "ui::button_label_styled") &&
           nk_button_text_styled(ctx, &s, ui_text(text), ui_len(text, "ui::button_label_styled"));
}

uint8_t gs_ui_button_symbol_styled(gs_ui_context c, const gs_ui_style_button *style,
                                   int64_t symbol) {
    UI_WIDGET(c, "ui::button_symbol_styled", 0);
    struct nk_style_button s;
    return ui_symbol_ok(symbol, "ui::button_symbol_styled") &&
           ui_button_style(u, style, &s, "ui::button_symbol_styled") &&
           nk_button_symbol_styled(ctx, &s, (enum nk_symbol_type)symbol);
}

uint8_t gs_ui_button_image_styled(gs_ui_context c, const gs_ui_style_button *style,
                                  gs_ui_image img) {
    UI_WIDGET(c, "ui::button_image_styled", 0);
    struct nk_style_button s;
    return ui_button_style(u, style, &s, "ui::button_image_styled") &&
           nk_button_image_styled(ctx, &s, ui_nk_image(img));
}

uint8_t gs_ui_button_symbol_label_styled(gs_ui_context c, const gs_ui_style_button *style,
                                         int64_t symbol, gs_ui_bytes text, int64_t align) {
    UI_WIDGET(c, "ui::button_symbol_label_styled", 0);
    struct nk_style_button s;
    return ui_symbol_ok(symbol, "ui::button_symbol_label_styled") &&
           ui_button_style(u, style, &s, "ui::button_symbol_label_styled") &&
           nk_button_symbol_text_styled(ctx, &s, (enum nk_symbol_type)symbol, ui_text(text),
                                        ui_len(text, "ui::button_symbol_label_styled"),
                                        (nk_flags)align);
}

uint8_t gs_ui_button_image_label_styled(gs_ui_context c, const gs_ui_style_button *style,
                                        gs_ui_image img, gs_ui_bytes text, int64_t align) {
    UI_WIDGET(c, "ui::button_image_label_styled", 0);
    struct nk_style_button s;
    return ui_button_style(u, style, &s, "ui::button_image_label_styled") &&
           nk_button_image_text_styled(ctx, &s, ui_nk_image(img), ui_text(text),
                                       ui_len(text, "ui::button_image_label_styled"),
                                       (nk_flags)align);
}

static bool ui_behavior_ok(int64_t behavior, const char *fn) {
    return ui_enum_ok(behavior, 2, fn, "BUTTON_DEFAULT, BUTTON_REPEATER");
}

void gs_ui_button_set_behavior(gs_ui_context c, int64_t behavior) {
    UI_CTX(c, "ui::button_set_behavior", );
    if (ui_behavior_ok(behavior, "ui::button_set_behavior"))
        nk_button_set_behavior(ctx, (enum nk_button_behavior)behavior);
}

uint8_t gs_ui_button_push_behavior(gs_ui_context c, int64_t behavior) {
    UI_CTX(c, "ui::button_push_behavior", 0);
    if (!ui_behavior_ok(behavior, "ui::button_push_behavior")) return 0;
    if (ctx->stacks.button_behaviors.head >= NK_BUTTON_BEHAVIOR_STACK_SIZE)
        return ui_misuse("ui::button_push_behavior: %d pushed and not popped",
                         NK_BUTTON_BEHAVIOR_STACK_SIZE);
    return nk_button_push_behavior(ctx, (enum nk_button_behavior)behavior) != 0;
}

uint8_t gs_ui_button_pop_behavior(gs_ui_context c) {
    UI_CTX(c, "ui::button_pop_behavior", 0);
    if (ctx->stacks.button_behaviors.head < 1)
        return ui_misuse("ui::button_pop_behavior with none pushed");
    return nk_button_pop_behavior(ctx) != 0;
}

/* --- checkboxes, radio buttons, selectables ----------------------------------------- */

uint8_t gs_ui_check_label(gs_ui_context c, gs_ui_bytes text, uint8_t active) {
    UI_WIDGET(c, "ui::check_label", active);
    return nk_check_text(ctx, ui_text(text), ui_len(text, "ui::check_label"), active != 0) != 0;
}

uint8_t gs_ui_check_label_align(gs_ui_context c, gs_ui_bytes text, uint8_t active,
                                int64_t widget_align, int64_t text_align) {
    UI_WIDGET(c, "ui::check_label_align", active);
    return nk_check_text_align(ctx, ui_text(text), ui_len(text, "ui::check_label_align"),
                               active != 0, (nk_flags)widget_align, (nk_flags)text_align) != 0;
}

int64_t gs_ui_check_flags_label(gs_ui_context c, gs_ui_bytes text, int64_t flags, int64_t value) {
    UI_WIDGET(c, "ui::check_flags_label", flags);
    return (int64_t)nk_check_flags_text(ctx, ui_text(text), ui_len(text, "ui::check_flags_label"),
                                        (unsigned)flags, (unsigned)value);
}

uint8_t gs_ui_checkbox_label(gs_ui_context c, gs_ui_bytes text, uint8_t *active) {
    UI_WIDGET(c, "ui::checkbox_label", 0);
    nk_bool a = *active != 0;
    nk_bool r = nk_checkbox_text(ctx, ui_text(text), ui_len(text, "ui::checkbox_label"), &a);
    *active = a != 0;
    return r != 0;
}

uint8_t gs_ui_checkbox_label_align(gs_ui_context c, gs_ui_bytes text, uint8_t *active,
                                   int64_t widget_align, int64_t text_align) {
    UI_WIDGET(c, "ui::checkbox_label_align", 0);
    nk_bool a = *active != 0;
    nk_bool r = nk_checkbox_text_align(ctx, ui_text(text), ui_len(text, "ui::checkbox_label_align"),
                                       &a, (nk_flags)widget_align, (nk_flags)text_align);
    *active = a != 0;
    return r != 0;
}

uint8_t gs_ui_checkbox_flags_label(gs_ui_context c, gs_ui_bytes text, int64_t *flags,
                                   int64_t value) {
    UI_WIDGET(c, "ui::checkbox_flags_label", 0);
    unsigned f = (unsigned)*flags;
    nk_bool r = nk_checkbox_flags_text(ctx, ui_text(text), ui_len(text, "ui::checkbox_flags_label"),
                                       &f, (unsigned)value);
    *flags = (int64_t)(uint32_t)f;
    return r != 0;
}

uint8_t gs_ui_radio_label(gs_ui_context c, gs_ui_bytes text, uint8_t *active) {
    UI_WIDGET(c, "ui::radio_label", 0);
    nk_bool a = *active != 0;
    nk_bool r = nk_radio_text(ctx, ui_text(text), ui_len(text, "ui::radio_label"), &a);
    *active = a != 0;
    return r != 0;
}

uint8_t gs_ui_radio_label_align(gs_ui_context c, gs_ui_bytes text, uint8_t *active,
                                int64_t widget_align, int64_t text_align) {
    UI_WIDGET(c, "ui::radio_label_align", 0);
    nk_bool a = *active != 0;
    nk_bool r = nk_radio_text_align(ctx, ui_text(text), ui_len(text, "ui::radio_label_align"), &a,
                                    (nk_flags)widget_align, (nk_flags)text_align);
    *active = a != 0;
    return r != 0;
}

uint8_t gs_ui_option_label(gs_ui_context c, gs_ui_bytes text, uint8_t active) {
    UI_WIDGET(c, "ui::option_label", active);
    return nk_option_text(ctx, ui_text(text), ui_len(text, "ui::option_label"), active != 0) != 0;
}

uint8_t gs_ui_option_label_align(gs_ui_context c, gs_ui_bytes text, uint8_t active,
                                 int64_t widget_align, int64_t text_align) {
    UI_WIDGET(c, "ui::option_label_align", active);
    return nk_option_text_align(ctx, ui_text(text), ui_len(text, "ui::option_label_align"),
                                active != 0, (nk_flags)widget_align, (nk_flags)text_align) != 0;
}

uint8_t gs_ui_selectable_label(gs_ui_context c, gs_ui_bytes text, int64_t align, uint8_t *value) {
    UI_WIDGET(c, "ui::selectable_label", 0);
    nk_bool v = *value != 0;
    nk_bool r = nk_selectable_text(ctx, ui_text(text), ui_len(text, "ui::selectable_label"),
                                   (nk_flags)align, &v);
    *value = v != 0;
    return r != 0;
}

uint8_t gs_ui_selectable_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                     int64_t align, uint8_t *value) {
    UI_WIDGET(c, "ui::selectable_image_label", 0);
    nk_bool v = *value != 0;
    nk_bool r = nk_selectable_image_text(ctx, ui_nk_image(img), ui_text(text),
                                         ui_len(text, "ui::selectable_image_label"),
                                         (nk_flags)align, &v);
    *value = v != 0;
    return r != 0;
}

uint8_t gs_ui_selectable_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                      int64_t align, uint8_t *value) {
    UI_WIDGET(c, "ui::selectable_symbol_label", 0);
    if (!ui_symbol_ok(symbol, "ui::selectable_symbol_label")) return 0;
    nk_bool v = *value != 0;
    nk_bool r = nk_selectable_symbol_text(ctx, (enum nk_symbol_type)symbol, ui_text(text),
                                          ui_len(text, "ui::selectable_symbol_label"),
                                          (nk_flags)align, &v);
    *value = v != 0;
    return r != 0;
}

uint8_t gs_ui_select_label(gs_ui_context c, gs_ui_bytes text, int64_t align, uint8_t value) {
    UI_WIDGET(c, "ui::select_label", value);
    return nk_select_text(ctx, ui_text(text), ui_len(text, "ui::select_label"), (nk_flags)align,
                          value != 0) != 0;
}

uint8_t gs_ui_select_image_label(gs_ui_context c, gs_ui_image img, gs_ui_bytes text,
                                 int64_t align, uint8_t value) {
    UI_WIDGET(c, "ui::select_image_label", value);
    return nk_select_image_text(ctx, ui_nk_image(img), ui_text(text),
                                ui_len(text, "ui::select_image_label"), (nk_flags)align,
                                value != 0) != 0;
}

uint8_t gs_ui_select_symbol_label(gs_ui_context c, int64_t symbol, gs_ui_bytes text,
                                  int64_t align, uint8_t value) {
    UI_WIDGET(c, "ui::select_symbol_label", value);
    return ui_symbol_ok(symbol, "ui::select_symbol_label") &&
           nk_select_symbol_text(ctx, (enum nk_symbol_type)symbol, ui_text(text),
                                 ui_len(text, "ui::select_symbol_label"), (nk_flags)align,
                                 value != 0);
}

/* --- sliders, knobs, progress bars, color pickers ------------------------------------ */

float gs_ui_slide_float(gs_ui_context c, float min, float value, float max, float step) {
    UI_WIDGET(c, "ui::slide_float", value);
    if (!ui_span_ok(min, max, step, "ui::slide_float") ||
        !ui_finite(value, "ui::slide_float", "a value of"))
        return value;
    return nk_slide_float(ctx, min, value, max, step);
}

int64_t gs_ui_slide_int(gs_ui_context c, int64_t min, int64_t value, int64_t max, int64_t step) {
    UI_WIDGET(c, "ui::slide_int", value);
    if (!ui_span_ok((double)min, (double)max, (double)step, "ui::slide_int")) return value;
    return nk_slide_int(ctx, ui_int(min), ui_int(value), ui_int(max), ui_int(step));
}

uint8_t gs_ui_slider_float(gs_ui_context c, float min, float *value, float max, float step) {
    UI_WIDGET(c, "ui::slider_float", 0);
    if (!ui_span_ok(min, max, step, "ui::slider_float") ||
        !ui_finite(*value, "ui::slider_float", "a value of"))
        return 0;
    return nk_slider_float(ctx, min, value, max, step) != 0;
}

uint8_t gs_ui_slider_int(gs_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step) {
    UI_WIDGET(c, "ui::slider_int", 0);
    if (!ui_span_ok((double)min, (double)max, (double)step, "ui::slider_int")) return 0;
    int v = ui_int(*value);
    nk_bool r = nk_slider_int(ctx, ui_int(min), &v, ui_int(max), ui_int(step));
    *value = v;
    return r != 0;
}

static bool ui_knob_ok(int64_t zero_direction, float dead_zone, const char *fn) {
    if (!ui_enum_ok(zero_direction, 4, fn, "UP, RIGHT, DOWN, LEFT")) return false;
    if (dead_zone >= 0 && dead_zone < 360) return true;
    return ui_misuse("%s: a dead zone of %g degrees (0 to 360)", fn, (double)dead_zone);
}

uint8_t gs_ui_knob_float(gs_ui_context c, float min, float *value, float max, float step,
                         int64_t zero_direction, float dead_zone_degrees) {
    UI_WIDGET(c, "ui::knob_float", 0);
    if (!ui_span_ok(min, max, step, "ui::knob_float") ||
        !ui_finite(*value, "ui::knob_float", "a value of") ||
        !ui_knob_ok(zero_direction, dead_zone_degrees, "ui::knob_float"))
        return 0;
    return nk_knob_float(ctx, min, value, max, step, (enum nk_heading)zero_direction,
                         dead_zone_degrees) != 0;
}

uint8_t gs_ui_knob_int(gs_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step,
                       int64_t zero_direction, float dead_zone_degrees) {
    UI_WIDGET(c, "ui::knob_int", 0);
    if (!ui_span_ok((double)min, (double)max, (double)step, "ui::knob_int") ||
        !ui_knob_ok(zero_direction, dead_zone_degrees, "ui::knob_int"))
        return 0;
    int v = ui_int(*value);
    nk_bool r = nk_knob_int(ctx, ui_int(min), &v, ui_int(max), ui_int(step),
                            (enum nk_heading)zero_direction, dead_zone_degrees);
    *value = v;
    return r != 0;
}

uint8_t gs_ui_progress(gs_ui_context c, int64_t *cur, int64_t max, uint8_t modifiable) {
    UI_WIDGET(c, "ui::progress", 0);
    if (max < 0 || *cur < 0) {
        ui_misuse("ui::progress: %lld of %lld", (long long)*cur, (long long)max);
        return 0;
    }
    /* Nuklear sizes its bar by the value before it clamps it to max. */
    nk_size v = (nk_size)(*cur < max ? *cur : max);
    nk_progress(ctx, &v, (nk_size)max, modifiable != 0);
    bool changed = (int64_t)v != *cur;
    *cur = (int64_t)v;
    return changed;
}

int64_t gs_ui_prog(gs_ui_context c, int64_t cur, int64_t max, uint8_t modifiable) {
    UI_WIDGET(c, "ui::prog", cur);
    if (max < 0 || cur < 0) {
        ui_misuse("ui::prog: %lld of %lld", (long long)cur, (long long)max);
        return cur;
    }
    return (int64_t)nk_prog(ctx, (nk_size)(cur < max ? cur : max), (nk_size)max, modifiable != 0);
}

static bool ui_colorf_ok(gs_ui_colorf c, int64_t format, const char *fn) {
    if (!ui_enum_ok(format, 2, fn, "RGB, RGBA")) return false;
    if (isfinite(c.r) && isfinite(c.g) && isfinite(c.b) && isfinite(c.a)) return true;
    return ui_misuse("%s: a color of %g, %g, %g, %g", fn, (double)c.r, (double)c.g, (double)c.b,
                     (double)c.a);
}

gs_ui_colorf gs_ui_color_picker(gs_ui_context c, gs_ui_colorf color, int64_t format) {
    UI_WIDGET(c, "ui::color_picker", color);
    if (!ui_colorf_ok(color, format, "ui::color_picker")) return color;
    return ui_colorf_of(nk_color_picker(ctx, ui_nk_colorf(color), (enum nk_color_format)format));
}

uint8_t gs_ui_color_pick(gs_ui_context c, gs_ui_colorf *color, int64_t format) {
    UI_WIDGET(c, "ui::color_pick", 0);
    if (!ui_colorf_ok(*color, format, "ui::color_pick")) return 0;
    struct nk_colorf n = ui_nk_colorf(*color);
    nk_bool r = nk_color_pick(ctx, &n, (enum nk_color_format)format);
    *color = ui_colorf_of(n);
    return r != 0;
}

/* --- properties ----------------------------------------------------------------------- */

static bool ui_property_ok(double min, double value, double max, double step, float inc,
                           const char *fn) {
    return ui_range_ok(min, max, step, fn) && ui_finite((float)value, fn, "a value of") &&
           ui_finite(inc, fn, "an increment per pixel of");
}

uint8_t gs_ui_property_int(gs_ui_context c, gs_ui_bytes name, int64_t min, int64_t *value,
                           int64_t max, int64_t step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::property_int", 0);
    if (!ui_property_ok((double)min, 0, (double)max, (double)step, inc_per_pixel,
                        "ui::property_int"))
        return 0;
    int v = ui_int(*value), old = v;
    nk_property_int(ctx, ui_cstr(name, 0), ui_int(min), &v, ui_int(max), ui_int(step),
                    inc_per_pixel);
    *value = v;
    return v != old;
}

uint8_t gs_ui_property_float(gs_ui_context c, gs_ui_bytes name, float min, float *value, float max,
                             float step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::property_float", 0);
    if (!ui_property_ok(min, *value, max, step, inc_per_pixel, "ui::property_float")) return 0;
    float old = *value;
    nk_property_float(ctx, ui_cstr(name, 0), min, value, max, step, inc_per_pixel);
    return *value != old;
}

uint8_t gs_ui_property_double(gs_ui_context c, gs_ui_bytes name, double min, double *value,
                              double max, double step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::property_double", 0);
    if (!ui_property_ok(min, isfinite(*value) ? 0 : *value, max, step, inc_per_pixel,
                        "ui::property_double"))
        return 0;
    double old = *value;
    nk_property_double(ctx, ui_cstr(name, 0), min, value, max, step, inc_per_pixel);
    return *value != old;
}

int64_t gs_ui_propertyi(gs_ui_context c, gs_ui_bytes name, int64_t min, int64_t value, int64_t max,
                        int64_t step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::propertyi", value);
    if (!ui_property_ok((double)min, 0, (double)max, (double)step, inc_per_pixel, "ui::propertyi"))
        return value;
    return nk_propertyi(ctx, ui_cstr(name, 0), ui_int(min), ui_int(value), ui_int(max),
                        ui_int(step), inc_per_pixel);
}

float gs_ui_propertyf(gs_ui_context c, gs_ui_bytes name, float min, float value, float max,
                      float step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::propertyf", value);
    if (!ui_property_ok(min, value, max, step, inc_per_pixel, "ui::propertyf")) return value;
    return nk_propertyf(ctx, ui_cstr(name, 0), min, value, max, step, inc_per_pixel);
}

double gs_ui_propertyd(gs_ui_context c, gs_ui_bytes name, double min, double value, double max,
                       double step, float inc_per_pixel) {
    UI_WIDGET(c, "ui::propertyd", value);
    if (!ui_property_ok(min, isfinite(value) ? 0 : value, max, step, inc_per_pixel,
                        "ui::propertyd"))
        return value;
    return nk_propertyd(ctx, ui_cstr(name, 0), min, value, max, step, inc_per_pixel);
}

/* --- text editing ------------------------------------------------------------------------ */

nk_plugin_filter ui_filter(int64_t filter) {
    switch (filter) {
        case GS_UI_FILTER_DEFAULT: return nk_filter_default;
        case GS_UI_FILTER_ASCII: return nk_filter_ascii;
        case GS_UI_FILTER_FLOAT: return nk_filter_float;
        case GS_UI_FILTER_DECIMAL: return nk_filter_decimal;
        case GS_UI_FILTER_HEX: return nk_filter_hex;
        case GS_UI_FILTER_OCT: return nk_filter_oct;
        case GS_UI_FILTER_BINARY: return nk_filter_binary;
        default: return NULL;
    }
}

static nk_plugin_filter ui_filter_ok(int64_t filter, const char *fn) {
    nk_plugin_filter f = ui_filter(filter);
    if (!f) ui_misuse("%s: %lld is not one of the FILTER_* constants", fn, (long long)filter);
    return f;
}

uint8_t gs_ui_filter_accepts(int64_t filter, uint32_t rune) {
    nk_plugin_filter f = ui_filter_ok(filter, "ui::filter_accepts");
    if (!f) return 0;
    struct nk_text_edit none;
    memset(&none, 0, sizeof none);
    return f(&none, rune) != 0;
}

/* The byte offset of character i of s, or its length when i is past the
   last. */
static int ui_char_offset(const struct nk_str *s, int i) {
    const char *text = (const char *)s->buffer.memory.ptr;
    int n = (int)s->buffer.allocated, at = 0, g;
    nk_rune r;
    while (i-- > 0 && at < n && (g = nk_utf_decode(text + at, &r, n - at)) > 0) at += g;
    return at;
}

/* Nuklear moves the cursor over a character typed into a full buffer as if
   it went in: edit_string's filter turns away what would not fit, counting
   the selection or the character it replaces as free. */
static nk_plugin_filter ui_edit_filter;

static nk_bool ui_fit_filter(const struct nk_text_edit *e, nk_rune rune) {
    if (!ui_edit_filter(e, rune)) return nk_false;
    const struct nk_str *s = &e->string;
    int from = e->select_start < e->select_end ? e->select_start : e->select_end;
    int to = e->select_start < e->select_end ? e->select_end : e->select_start;
    if (from == to && e->mode == NK_TEXT_EDIT_MODE_REPLACE && e->cursor < s->len) {
        from = e->cursor;
        to = e->cursor + 1;
    }
    int room = (int)s->buffer.memory.size - 1 - (int)s->buffer.allocated +
               ui_char_offset(s, to) - ui_char_offset(s, from);
    char utf8[NK_UTF_SIZE];
    return nk_utf_encode(rune, utf8, NK_UTF_SIZE) <= room;
}

/* Edits go to a copy: Nuklear keeps pointing at the memory it edited. */
static char *ui_edit_scratch(int64_t n) {
    static char *buf;
    static int64_t cap;
    if (n > cap) {
        char *more = (char *)realloc(buf, (size_t)(n + 256));
        if (!more) {
            ui_fail("out of memory for a text field");
            return NULL;
        }
        buf = more;
        cap = n + 256;
    }
    return buf;
}

int64_t gs_ui_edit_string(gs_ui_context c, int64_t flags, gs_ui_bytes buffer, int64_t *len,
                          int64_t filter) {
    UI_WIDGET(c, "ui::edit_string", 0);
    nk_plugin_filter f = ui_filter_ok(filter, "ui::edit_string");
    if (!f) return 0;
    if (buffer.len < 1 || buffer.len > INT_MAX / 2 || *len < 0 || *len >= buffer.len) {
        ui_misuse("ui::edit_string: %lld bytes of text in a buffer of %lld, which holds %lld",
                  (long long)*len, (long long)buffer.len, (long long)(buffer.len - 1));
        return 0;
    }
    char *scratch = ui_edit_scratch(buffer.len);
    if (!scratch) return 0;
    memcpy(scratch, buffer.data, (size_t)buffer.len);
    int n = (int)*len;
    ui_edit_filter = f;
    nk_flags r = nk_edit_string(ctx, (nk_flags)flags, scratch, &n, (int)buffer.len, ui_fit_filter);
    memcpy(buffer.data, scratch, (size_t)n);
    *len = n;
    return (int64_t)r;
}

ui_table ui_text_edits = { "text edit" };

static ui_text_edit *ui_edit_get(gs_ui_text_edit e, const char *fn) {
    return (ui_text_edit *)ui_table_get(&ui_text_edits, e.id, fn);
}

int64_t gs_ui_edit_buffer(gs_ui_context c, int64_t flags, gs_ui_text_edit e, int64_t filter) {
    UI_WIDGET(c, "ui::edit_buffer", 0);
    ui_text_edit *t = ui_edit_get(e, "ui::edit_buffer");
    nk_plugin_filter f = t ? ui_filter_ok(filter, "ui::edit_buffer") : NULL;
    if (!f) return 0;
    nk_flags r = nk_edit_buffer(ctx, (nk_flags)flags, &t->edit, f);
    /* The window keeps the active edit's mode for the next frame, which
       nk_edit_buffer reads but, unlike nk_edit_string, does not write. */
    if (t->edit.active) ctx->current->edit.mode = t->edit.mode;
    return (int64_t)r;
}

void gs_ui_edit_focus(gs_ui_context c, int64_t flags) {
    UI_WINDOW(c, "ui::edit_focus", );
    nk_edit_focus(ctx, (nk_flags)flags);
}

void gs_ui_edit_unfocus(gs_ui_context c) {
    UI_WINDOW(c, "ui::edit_unfocus", );
    nk_edit_unfocus(ctx);
}

gs_ui_text_edit gs_ui_create_text_edit(void) {
    gs_ui_text_edit h = { 0 };
    ui_text_edit *t = (ui_text_edit *)calloc(1, sizeof *t);
    if (!t) {
        ui_fail("out of memory for a text edit");
        return h;
    }
    nk_textedit_init_default(&t->edit);
    t->id = ui_table_add(&ui_text_edits, t);
    if (!t->id) {
        nk_textedit_free(&t->edit);
        free(t);
        return h;
    }
    h.id = t->id;
    return h;
}

void gs_ui_destroy_text_edit(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::destroy");
    if (!t) return;
    ui_table_remove(&ui_text_edits, e.id);
    nk_textedit_free(&t->edit);
    free(t);
}

uint8_t gs_ui_text_edit_is_valid(gs_ui_text_edit e) {
    return ui_table_find(&ui_text_edits, e.id) != NULL;
}

int64_t gs_ui_text_edit_text(gs_ui_text_edit e, gs_ui_bytes out) {
    ui_text_edit *t = ui_edit_get(e, "ui::text");
    if (!t) return 0;
    const char *s = nk_str_get_const(&t->edit.string);
    return ui_copy_out(s, s ? nk_str_len_char(&t->edit.string) : 0, out);
}

void gs_ui_text_edit_set_text(gs_ui_text_edit e, gs_ui_bytes text) {
    ui_text_edit *t = ui_edit_get(e, "ui::set_text");
    if (!t) return;
    struct nk_text_edit *s = &t->edit;
    nk_str_clear(&s->string);
    nk_str_append_text_char(&s->string, ui_text(text), ui_len(text, "ui::set_text"));
    /* A new text: nothing selected, the cursor at its start, no undo. */
    s->cursor = s->select_start = s->select_end = 0;
    s->has_preferred_x = 0;
    s->undo.undo_point = 0;
    s->undo.undo_char_point = 0;
    s->undo.redo_point = NK_TEXTEDIT_UNDOSTATECOUNT;
    s->undo.redo_char_point = NK_TEXTEDIT_UNDOCHARCOUNT;
}

gs_ui_text_edit_state gs_ui_text_edit_state_of(gs_ui_text_edit e) {
    gs_ui_text_edit_state g;
    memset(&g, 0, sizeof g);
    ui_text_edit *t = ui_edit_get(e, "ui::state");
    if (!t) return g;
    g.cursor = t->edit.cursor;
    g.select_start = t->edit.select_start;
    g.select_end = t->edit.select_end;
    g.mode = t->edit.mode;
    g.length = t->edit.string.len;
    g.single_line = t->edit.single_line;
    g.active = t->edit.active;
    return g;
}

void gs_ui_text_edit_set_cursor(gs_ui_text_edit e, int64_t cursor, int64_t select_start,
                                int64_t select_end) {
    ui_text_edit *t = ui_edit_get(e, "ui::set_cursor");
    if (!t) return;
    int64_t n = t->edit.string.len;
    if (cursor < 0 || cursor > n || select_start < 0 || select_start > n || select_end < 0 ||
        select_end > n) {
        ui_misuse("ui::set_cursor: the cursor at %lld, the selection %lld to %lld, in %lld "
                  "characters", (long long)cursor, (long long)select_start, (long long)select_end,
                  (long long)n);
        return;
    }
    t->edit.cursor = (int)cursor;
    t->edit.select_start = (int)select_start;
    t->edit.select_end = (int)select_end;
    t->edit.has_preferred_x = 0;
}

void gs_ui_text_edit_set_mode(gs_ui_text_edit e, int64_t mode) {
    ui_text_edit *t = ui_edit_get(e, "ui::set_mode");
    if (t && ui_enum_ok(mode, 3, "ui::set_mode", "TEXT_EDIT_MODE_*"))
        t->edit.mode = (unsigned char)mode;
}

void gs_ui_textedit_text(gs_ui_text_edit e, gs_ui_bytes text) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_text");
    if (t && text.len) nk_textedit_text(&t->edit, ui_text(text), ui_len(text, "ui::textedit_text"));
}

void gs_ui_textedit_delete(gs_ui_text_edit e, int64_t where, int64_t len) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_delete");
    if (!t) return;
    int64_t n = t->edit.string.len;
    if (where < 0 || len < 0 || where > n || len > n - where) {
        ui_misuse("ui::textedit_delete: %lld characters from %lld, of %lld", (long long)len,
                  (long long)where, (long long)n);
        return;
    }
    if (len) nk_textedit_delete(&t->edit, (int)where, (int)len);
}

void gs_ui_textedit_delete_selection(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_delete_selection");
    if (t) nk_textedit_delete_selection(&t->edit);
}

void gs_ui_textedit_select_all(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_select_all");
    if (t) nk_textedit_select_all(&t->edit);
}

uint8_t gs_ui_textedit_cut(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_cut");
    return t && nk_textedit_cut(&t->edit);
}

uint8_t gs_ui_textedit_paste(gs_ui_text_edit e, gs_ui_bytes text) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_paste");
    return t && ui_textedit_paste(&t->edit, ui_text(text), ui_len(text, "ui::textedit_paste"));
}

void gs_ui_textedit_undo(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_undo");
    if (t) nk_textedit_undo(&t->edit);
}

void gs_ui_textedit_redo(gs_ui_text_edit e) {
    ui_text_edit *t = ui_edit_get(e, "ui::textedit_redo");
    if (t) nk_textedit_redo(&t->edit);
}

/* --- charts --------------------------------------------------------------------------------- */

static bool ui_chart_ok(int64_t type, int64_t count, float min, float max, const char *fn) {
    if (!ui_enum_ok(type, 2, fn, "CHART_LINES, CHART_COLUMN")) return false;
    if (count < 0 || count > INT_MAX)
        return ui_misuse("%s: %lld values", fn, (long long)count);
    if (isfinite(min) && isfinite(max) && min != max) return true;
    return ui_misuse("%s: values from %g to %g, where the range must not be empty", fn,
                     (double)min, (double)max);
}

uint8_t gs_ui_chart_begin(gs_ui_context c, int64_t type, int64_t count, float min, float max) {
    UI_WIDGET(c, "ui::chart_begin", 0);
    if (!ui_chart_ok(type, count, min, max, "ui::chart_begin")) return 0;
    return nk_chart_begin(ctx, (enum nk_chart_type)type, (int)count, min, max) &&
           ui_push_scope(u, UI_SCOPE_CHART, true, "ui::chart_begin");
}

uint8_t gs_ui_chart_begin_colored(gs_ui_context c, int64_t type, gs_ui_color color,
                                  gs_ui_color active, int64_t count, float min, float max) {
    UI_WIDGET(c, "ui::chart_begin_colored", 0);
    if (!ui_chart_ok(type, count, min, max, "ui::chart_begin_colored")) return 0;
    return nk_chart_begin_colored(ctx, (enum nk_chart_type)type, ui_nk_color(color),
                                  ui_nk_color(active), (int)count, min, max) &&
           ui_push_scope(u, UI_SCOPE_CHART, true, "ui::chart_begin_colored");
}

/* A slot added to the chart begun last, up to Nuklear's four. */
static bool ui_chart_slot_ok(ui_ctx *u, const char *fn) {
    if (!ui_in_panel(u, fn) || !ui_expect_scope(u, UI_SCOPE_CHART, fn)) return false;
    if (u->nk.current->layout->chart.slot < NK_CHART_MAX_SLOT) return true;
    return ui_misuse("%s: a chart has at most %d slots", fn, NK_CHART_MAX_SLOT);
}

void gs_ui_chart_add_slot(gs_ui_context c, int64_t type, int64_t count, float min, float max) {
    UI_CTX(c, "ui::chart_add_slot", );
    if (ui_chart_slot_ok(u, "ui::chart_add_slot") &&
        ui_chart_ok(type, count, min, max, "ui::chart_add_slot"))
        nk_chart_add_slot(ctx, (enum nk_chart_type)type, (int)count, min, max);
}

void gs_ui_chart_add_slot_colored(gs_ui_context c, int64_t type, gs_ui_color color,
                                  gs_ui_color active, int64_t count, float min, float max) {
    UI_CTX(c, "ui::chart_add_slot_colored", );
    if (ui_chart_slot_ok(u, "ui::chart_add_slot_colored") &&
        ui_chart_ok(type, count, min, max, "ui::chart_add_slot_colored"))
        nk_chart_add_slot_colored(ctx, (enum nk_chart_type)type, ui_nk_color(color),
                                  ui_nk_color(active), (int)count, min, max);
}

int64_t gs_ui_chart_push_slot(gs_ui_context c, float value, int64_t slot) {
    UI_PANEL(c, "ui::chart_push_slot", 0);
    if (!ui_expect_scope(u, UI_SCOPE_CHART, "ui::chart_push_slot") ||
        !ui_finite(value, "ui::chart_push_slot", "a value of"))
        return 0;
    if (slot < 0 || slot >= ctx->current->layout->chart.slot) {
        ui_misuse("ui::chart_push_slot: slot %lld of a chart with %d", (long long)slot,
                  ctx->current->layout->chart.slot);
        return 0;
    }
    return (int64_t)nk_chart_push_slot(ctx, value, (int)slot);
}

int64_t gs_ui_chart_push(gs_ui_context c, float value) {
    return gs_ui_chart_push_slot(c, value, 0);
}

void gs_ui_chart_end(gs_ui_context c) {
    UI_PANEL(c, "ui::chart_end", );
    if (!ui_expect_scope(u, UI_SCOPE_CHART, "ui::chart_end")) return;
    nk_chart_end(ctx);
    ui_pop_scope(u);
}

void gs_ui_plot(gs_ui_context c, int64_t type, gs_ui_f32_slice values) {
    UI_WIDGET(c, "ui::plot", );
    if (!ui_enum_ok(type, 2, "ui::plot", "CHART_LINES, CHART_COLUMN") || !values.len) return;
    if (values.len > INT_MAX) {
        ui_misuse("ui::plot: %lld values", (long long)values.len);
        return;
    }
    float lo = values.data[0], hi = values.data[0];
    for (int64_t i = 0; i < values.len; i++) {
        if (!ui_finite(values.data[i], "ui::plot", "a value of")) return;
        lo = values.data[i] < lo ? values.data[i] : lo;
        hi = values.data[i] > hi ? values.data[i] : hi;
    }
    /* Nuklear's own plot divides by the range, which equal values leave
       empty: a flat line then sits at the bottom of a range of 1. */
    if (hi == lo) hi = lo + 1;
    if (nk_chart_begin(ctx, (enum nk_chart_type)type, (int)values.len, lo, hi)) {
        for (int64_t i = 0; i < values.len; i++) nk_chart_push(ctx, values.data[i]);
        nk_chart_end(ctx);
    }
}
