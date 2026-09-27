/* The ui layer's C API: every function stdlib/ui.goose reaches through an
   `extern "gs_ui_..." fn`, listed once in GS_UI_API. That list expands into
   the prototypes below and into the symbol table a JIT run registers
   (src/jit.h), and the test suite checks stdlib/ui.goose against it
   (test/api_check.py).

   The layer sits over Nuklear (third_party/nuklear), an immediate-mode GUI
   library, and changes little of it: nk_button_label is gs_ui_button_label,
   and ui::button_label in Goose. What it does change is what cannot cross
   an `extern fn` (spec 7.10):

   * Strings are slices, never NUL-terminated. Where Nuklear has a `_label`
     function taking a C string and a `_text` one taking a length, the layer
     has only the `_label` name, and passes the length on.
   * Pointers Nuklear keeps are kept by the layer instead: fonts' TTF data
     and glyph ranges, a layout row's ratios, cursors, scroll offsets a
     group writes back when it ends, the text a paste inserts. The program
     never lends memory beyond a call.
   * Nuklear's objects are handles: a context, a font atlas and its fonts, a
     text editor. Each is a slot index with a generation, so a destroyed one
     is an error to use, not a crash.
   * Callbacks are gone: text filters are picked from Nuklear's own set
     (FILTER_*), the clipboard is text the program hands over and takes
     back, and what Nuklear draws comes back as arrays -- its command list,
     or vertices, indices and draw calls to render with any GPU API.
   * Style is one Goose-shaped struct, read and written whole, in place of
     the pointers into nk_style that Nuklear's push and pop functions take.

   The layer has no GPU and no window: stdlib/ui.goose renders what comes
   back through the gfx module, and feeds the context gfx's input. An image
   is any u32 a renderer understands, which for that renderer is a gfx
   texture handle.

   Errors: a function that can fail for reasons outside the program (a font
   file that is not there) returns false or a zero handle, with the reason
   in gs_ui_error. A call the program should not have made (a widget outside
   a window, a group ended that was never begun, a key that does not exist,
   a stale handle, one of Nuklear's own asserts) is skipped, counted by
   gs_ui_misuse_count, and turned into an abort by the Goose side at the
   next frame's input, render or check. Everything is main-thread only.

   The structs are packed and Goose-shaped: each is declared again in
   stdlib/ui.goose with the same fields in the same order, `Rect` there
   being gs_ui_rect here and a slice of `Color` gs_ui_color_slice. */

#ifndef GS_UI_API_H
#define GS_UI_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* --- slices, vectors and handles ------------------------------------------ */

typedef struct { uint8_t *data; int64_t len; } gs_ui_bytes;   /* u8[:], the generated sl_u8 */

typedef struct { int32_t x, y; } gs_ui_int2;
typedef struct { float x, y; } gs_ui_float2;
typedef struct { float x, y, z; } gs_ui_float3;
typedef struct { float x, y, z, w; } gs_ui_float4;

typedef struct { uint32_t id; } gs_ui_context;
typedef struct { uint32_t id; } gs_ui_font_atlas;
typedef struct { uint32_t id; } gs_ui_font;
typedef struct { uint32_t id; } gs_ui_text_edit;

typedef struct { float x, y, w, h; } gs_ui_rect;
typedef struct { uint8_t r, g, b, a; } gs_ui_color;
typedef struct { float r, g, b, a; } gs_ui_colorf;
typedef struct { uint32_t x, y; } gs_ui_scroll;

/* A texture, or a region of one: `texture` is whatever the renderer draws
   with (a gfx texture handle for stdlib/ui.goose's), w and h its size and
   region x, y, w, h the part shown, for a subimage. */
typedef struct { uint32_t texture; uint16_t w, h; uint16_t region[4]; } gs_ui_image;
/* An image drawn stretched in nine parts: l, t, r, b are the widths of the
   borders that are not stretched. */
typedef struct { gs_ui_image img; uint16_t l, t, r, b; } gs_ui_nine_slice;
/* A software cursor: an image with its size and hot spot. */
typedef struct { gs_ui_image img; gs_ui_float2 size, offset; } gs_ui_cursor;

typedef struct { gs_ui_float2 *data; int64_t len; } gs_ui_float2_slice;
typedef struct { gs_ui_int2 *data; int64_t len; } gs_ui_int2_slice;
typedef struct { float *data; int64_t len; } gs_ui_f32_slice;
typedef struct { uint32_t *data; int64_t len; } gs_ui_u32_slice;
typedef struct { gs_ui_color *data; int64_t len; } gs_ui_color_slice;
typedef struct { gs_ui_cursor *data; int64_t len; } gs_ui_cursor_slice;
typedef struct { gs_ui_font *data; int64_t len; } gs_ui_font_slice;

/* --- fonts ----------------------------------------------------------------- */

/* How a font is baked into its atlas (nk_font_config, less its pointers). */
typedef struct {
    uint8_t oversample_h, oversample_v;     /* rasterized larger, for sub-pixel positions */
    uint8_t pixel_snap;                     /* glyphs on whole pixels */
    uint8_t merge_mode;                     /* glyphs added to the font added before */
    gs_ui_float2 spacing;                   /* extra space between glyphs */
    int32_t ranges;                         /* a RANGE_* set of characters, unless given */
    uint32_t fallback_glyph;                /* drawn for a character the font lacks */
} gs_ui_font_config;

typedef struct {
    float height, ascent, descent;
    int32_t glyph_count;
    uint32_t fallback_codepoint;
} gs_ui_font_info;

typedef struct {
    uint32_t codepoint;
    float xadvance;
    float x0, y0, x1, y1, w, h;
    float u0, v0, u1, v1;
} gs_ui_font_glyph;

/* --- input, output ---------------------------------------------------------- */

typedef struct {
    gs_ui_float2 pos, prev, delta, scroll_delta;
    uint8_t grab, grabbed, ungrab;
} gs_ui_mouse;

/* One command of what the widgets drew, flattened: the fields a kind has
   are the ones that mean something for it. */
typedef struct {
    int32_t kind;                   /* COMMAND_* */
    int32_t x, y, w, h;             /* scissor, rect, circle, image, text: the box; arc: its center and radius in x, y, w */
    int32_t line_thickness, rounding;
    gs_ui_int2 points[4];           /* line: begin, end; curve: begin, two controls, end; triangle: its corners */
    float a_min, a_max;             /* arc: its angles */
    gs_ui_color color;              /* stroke, fill or text color; image: its tint */
    gs_ui_color background;         /* text: what it is drawn on */
    gs_ui_color left, top, right, bottom;   /* rect_multi_color: each side's */
    int32_t first, count;           /* polygon, polyline: points in gs_ui_command_points; text: bytes in gs_ui_command_text */
    gs_ui_image image;              /* image */
    gs_ui_font font;                /* text */
    float height;                   /* text: the font's height */
} gs_ui_command;
typedef struct { gs_ui_command *data; int64_t len; } gs_ui_command_slice;

/* How the commands become triangles (nk_convert_config, less its layout,
   which is always gs_ui_vertex). */
typedef struct {
    float global_alpha;
    uint8_t line_aa, shape_aa;      /* anti-aliased edges */
    int32_t circle_segment_count, arc_segment_count, curve_segment_count;
} gs_ui_convert_config;

typedef struct { gs_ui_float2 pos, uv; gs_ui_color color; } gs_ui_vertex;
typedef struct { gs_ui_vertex *data; int64_t len; } gs_ui_vertex_slice;
/* elem_count indices from where the draw before it stopped, with this
   scissor and texture. */
typedef struct { uint32_t elem_count; gs_ui_rect clip_rect; uint32_t texture; } gs_ui_draw_command;
typedef struct { gs_ui_draw_command *data; int64_t len; } gs_ui_draw_command_slice;

typedef struct { int64_t begin, end, count; } gs_ui_list_view;

typedef struct {
    int64_t cursor, select_start, select_end;   /* in characters */
    int64_t mode;                               /* TEXT_EDIT_MODE_* */
    int64_t length;                             /* in characters */
    uint8_t single_line, active;
} gs_ui_text_edit_state;

/* --- style: nk_style and its parts, less callbacks, fonts and cursors ------- */

/* A background: a color, an image, a nine-slice (img with the borders l, t,
   r, b), or nothing, by `kind` (STYLE_ITEM_*). */
typedef struct {
    int32_t kind;
    gs_ui_color color;
    gs_ui_image img;
    uint16_t l, t, r, b;
} gs_ui_style_item;

typedef struct {
    gs_ui_color color;
    gs_ui_float2 padding;
    float color_factor, disabled_factor;
} gs_ui_style_text;

typedef struct {
    gs_ui_color text_normal, text_hover, text_active;
    int32_t underline;
    float underline_thickness;
    gs_ui_float2 padding, touch_padding;
    float color_factor, disabled_factor;
} gs_ui_style_link;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    float color_factor_background;
    gs_ui_color text_background, text_normal, text_hover, text_active;
    uint32_t text_alignment;
    float color_factor_text;
    float border, rounding;
    gs_ui_float2 padding, image_padding, touch_padding;
    float disabled_factor;
} gs_ui_style_button;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_style_item cursor_normal, cursor_hover;
    gs_ui_color text_normal, text_hover, text_active, text_background;
    uint32_t text_alignment;
    gs_ui_float2 padding, touch_padding;
    float spacing, border, color_factor, disabled_factor;
} gs_ui_style_toggle;

typedef struct {
    gs_ui_style_item normal, hover, pressed;
    gs_ui_style_item normal_active, hover_active, pressed_active;
    gs_ui_color text_normal, text_hover, text_pressed;
    gs_ui_color text_normal_active, text_hover_active, text_pressed_active;
    gs_ui_color text_background;
    uint32_t text_alignment;
    float rounding;
    gs_ui_float2 padding, touch_padding, image_padding;
    float color_factor, disabled_factor;
} gs_ui_style_selectable;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_color bar_normal, bar_hover, bar_active, bar_filled;
    gs_ui_style_item cursor_normal, cursor_hover, cursor_active;
    float border, rounding, bar_height;
    gs_ui_float2 padding, spacing, cursor_size;
    float color_factor, disabled_factor;
    uint8_t show_buttons;
    gs_ui_style_button inc_button, dec_button;
    int32_t inc_symbol, dec_symbol;
} gs_ui_style_slider;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_color knob_normal, knob_hover, knob_active, knob_border_color;
    gs_ui_color cursor_normal, cursor_hover, cursor_active;
    float border, knob_border;
    gs_ui_float2 padding, spacing;
    float cursor_width, color_factor, disabled_factor;
} gs_ui_style_knob;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_style_item cursor_normal, cursor_hover, cursor_active;
    gs_ui_color cursor_border_color;
    float rounding, border, cursor_border, cursor_rounding;
    gs_ui_float2 padding;
    float color_factor, disabled_factor;
} gs_ui_style_progress;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_style_item cursor_normal, cursor_hover, cursor_active;
    gs_ui_color cursor_border_color;
    float border, rounding, border_cursor, rounding_cursor;
    gs_ui_float2 padding;
    float color_factor, disabled_factor;
    uint8_t show_buttons;
    gs_ui_style_button inc_button, dec_button;
    int32_t inc_symbol, dec_symbol;
} gs_ui_style_scrollbar;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_style_scrollbar scrollbar;
    gs_ui_color cursor_normal, cursor_hover, cursor_text_normal, cursor_text_hover;
    gs_ui_color text_normal, text_hover, text_active;
    gs_ui_color selected_normal, selected_hover, selected_text_normal, selected_text_hover;
    float border, rounding, cursor_size;
    gs_ui_float2 scrollbar_size, padding;
    float row_padding, color_factor, disabled_factor;
} gs_ui_style_edit;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_color label_normal, label_hover, label_active;
    int32_t sym_left, sym_right;
    float border, rounding;
    gs_ui_float2 padding;
    float color_factor, disabled_factor;
    gs_ui_style_edit edit;
    gs_ui_style_button inc_button, dec_button;
} gs_ui_style_property;

typedef struct {
    gs_ui_style_item background;
    gs_ui_color border_color, selected_color, color;
    float border, rounding;
    gs_ui_float2 padding;
    float color_factor, disabled_factor;
    uint8_t show_markers;
} gs_ui_style_chart;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_color border_color;
    gs_ui_color label_normal, label_hover, label_active;
    gs_ui_color symbol_normal, symbol_hover, symbol_active;
    gs_ui_style_button button;
    int32_t sym_normal, sym_hover, sym_active;
    float border, rounding;
    gs_ui_float2 content_padding, button_padding, spacing;
    float color_factor, disabled_factor;
} gs_ui_style_combo;

typedef struct {
    gs_ui_style_item background;
    gs_ui_color border_color, text;
    gs_ui_style_button tab_maximize_button, tab_minimize_button;
    gs_ui_style_button node_maximize_button, node_minimize_button;
    int32_t sym_minimize, sym_maximize;
    float border, rounding, indent;
    gs_ui_float2 padding, spacing;
    float color_factor, disabled_factor;
} gs_ui_style_tab;

typedef struct {
    gs_ui_style_item normal, hover, active;
    gs_ui_style_button close_button, minimize_button;
    int32_t close_symbol, minimize_symbol, maximize_symbol;
    gs_ui_color label_normal, label_hover, label_active;
    int32_t align;
    gs_ui_float2 padding, label_padding, spacing;
} gs_ui_style_window_header;

typedef struct {
    gs_ui_style_window_header header;
    gs_ui_style_item fixed_background;
    gs_ui_color background;
    gs_ui_color border_color, popup_border_color, combo_border_color, contextual_border_color;
    gs_ui_color menu_border_color, group_border_color, tooltip_border_color;
    gs_ui_style_item scaler;
    float border, combo_border, contextual_border, menu_border, group_border, tooltip_border;
    float popup_border, min_row_height_padding;
    float rounding;
    gs_ui_float2 spacing, scrollbar_size, min_size;
    gs_ui_float2 padding, group_padding, popup_padding, combo_padding, contextual_padding;
    gs_ui_float2 menu_padding, tooltip_padding;
    int32_t tooltip_origin;
    gs_ui_float2 tooltip_offset;
    float tooltip_delay;
} gs_ui_style_window;

typedef struct {
    gs_ui_style_text text;
    gs_ui_style_link link;
    gs_ui_style_button button, contextual_button, menu_button;
    gs_ui_style_toggle option, checkbox;
    gs_ui_style_selectable selectable;
    gs_ui_style_slider slider;
    gs_ui_style_knob knob;
    gs_ui_style_progress progress;
    gs_ui_style_property property;
    gs_ui_style_edit edit;
    gs_ui_style_chart chart;
    gs_ui_style_scrollbar scrollh, scrollv;
    gs_ui_style_tab tab;
    gs_ui_style_combo combo;
    gs_ui_style_window window;
} gs_ui_style;

#pragma pack(pop)

#define GS_UI_API(X) \
    /* The library. */ \
    X(uint8_t, gs_ui_available, (void)) \
    X(int64_t, gs_ui_error, (gs_ui_bytes out)) \
    X(int64_t, gs_ui_misuse_count, (void)) \
    /* Font atlases and fonts. */ \
    X(gs_ui_font_atlas, gs_ui_create_font_atlas, (void)) \
    X(void, gs_ui_destroy_font_atlas, (gs_ui_font_atlas a)) \
    X(uint8_t, gs_ui_font_atlas_is_valid, (gs_ui_font_atlas a)) \
    X(gs_ui_font_config, gs_ui_default_font_config, (void)) \
    X(gs_ui_font, gs_ui_add_default_font, (gs_ui_font_atlas a, float height, gs_ui_font_config config, gs_ui_u32_slice ranges)) \
    X(gs_ui_font, gs_ui_add_font_from_memory, (gs_ui_font_atlas a, gs_ui_bytes ttf, float height, gs_ui_font_config config, gs_ui_u32_slice ranges)) \
    X(gs_ui_font, gs_ui_add_font_from_file, (gs_ui_font_atlas a, gs_ui_bytes path, float height, gs_ui_font_config config, gs_ui_u32_slice ranges)) \
    X(uint8_t, gs_ui_bake_font_atlas, (gs_ui_font_atlas a)) \
    X(uint8_t, gs_ui_font_atlas_is_baked, (gs_ui_font_atlas a)) \
    X(void, gs_ui_font_atlas_size, (gs_ui_font_atlas a, gs_ui_int2 *out)) \
    X(int64_t, gs_ui_font_atlas_pixels, (gs_ui_font_atlas a, gs_ui_bytes out)) \
    X(void, gs_ui_set_font_atlas_texture, (gs_ui_font_atlas a, uint32_t texture)) \
    X(uint32_t, gs_ui_font_atlas_texture, (gs_ui_font_atlas a)) \
    X(gs_ui_font_atlas, gs_ui_font_atlas_without_texture, (void)) \
    X(void, gs_ui_forget_font_atlas_textures, (void)) \
    X(int64_t, gs_ui_font_atlas_fonts, (gs_ui_font_atlas a, gs_ui_font_slice out)) \
    X(gs_ui_font_atlas, gs_ui_font_atlas_of, (gs_ui_font f)) \
    X(uint8_t, gs_ui_font_is_valid, (gs_ui_font f)) \
    X(gs_ui_font_info, gs_ui_font_info_of, (gs_ui_font f)) \
    X(uint8_t, gs_ui_font_find_glyph, (gs_ui_font f, uint32_t codepoint, gs_ui_font_glyph *out)) \
    X(float, gs_ui_font_text_width, (gs_ui_font f, gs_ui_bytes text)) \
    X(int64_t, gs_ui_font_glyph_ranges, (int64_t which, gs_ui_u32_slice out)) \
    /* Contexts. */ \
    X(gs_ui_context, gs_ui_create_context, (gs_ui_font font)) \
    X(gs_ui_context, gs_ui_create_default_context, (float font_height)) \
    X(gs_ui_font_atlas, gs_ui_context_atlas, (gs_ui_context c)) \
    X(void, gs_ui_destroy_context, (gs_ui_context c)) \
    X(uint8_t, gs_ui_context_is_valid, (gs_ui_context c)) \
    X(void, gs_ui_clear, (gs_ui_context c)) \
    X(void, gs_ui_set_delta_time, (gs_ui_context c, float seconds)) \
    X(float, gs_ui_delta_time, (gs_ui_context c)) \
    X(gs_ui_font, gs_ui_context_font, (gs_ui_context c)) \
    X(void, gs_ui_set_clipboard, (gs_ui_context c, gs_ui_bytes text)) \
    X(int64_t, gs_ui_copied, (gs_ui_context c, gs_ui_bytes out)) \
    X(uint32_t, gs_ui_null_texture, (gs_ui_context c)) \
    /* Input. */ \
    X(void, gs_ui_input_begin, (gs_ui_context c)) \
    X(void, gs_ui_input_motion, (gs_ui_context c, int64_t x, int64_t y)) \
    X(void, gs_ui_input_key, (gs_ui_context c, int64_t key, uint8_t down)) \
    X(void, gs_ui_input_button, (gs_ui_context c, int64_t button, int64_t x, int64_t y, uint8_t down)) \
    X(void, gs_ui_input_scroll, (gs_ui_context c, gs_ui_float2 amount)) \
    X(void, gs_ui_input_char, (gs_ui_context c, uint8_t ch)) \
    X(void, gs_ui_input_glyph, (gs_ui_context c, gs_ui_bytes utf8)) \
    X(void, gs_ui_input_unicode, (gs_ui_context c, uint32_t rune)) \
    X(void, gs_ui_input_end, (gs_ui_context c)) \
    X(gs_ui_mouse, gs_ui_input_mouse, (gs_ui_context c)) \
    X(uint8_t, gs_ui_input_has_mouse_click, (gs_ui_context c, int64_t button)) \
    X(uint8_t, gs_ui_input_has_mouse_click_in_rect, (gs_ui_context c, int64_t button, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_has_mouse_click_in_button_rect, (gs_ui_context c, int64_t button, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_has_mouse_click_down_in_rect, (gs_ui_context c, int64_t button, gs_ui_rect r, uint8_t down)) \
    X(uint8_t, gs_ui_input_is_mouse_click_in_rect, (gs_ui_context c, int64_t button, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_click_down_in_rect, (gs_ui_context c, int64_t button, gs_ui_rect r, uint8_t down)) \
    X(uint8_t, gs_ui_input_any_mouse_click_in_rect, (gs_ui_context c, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_prev_hovering_rect, (gs_ui_context c, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_hovering_rect, (gs_ui_context c, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_hovering_still_rect, (gs_ui_context c, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_hovering_delay_rect, (gs_ui_context c, gs_ui_rect r, float *timer, float delay)) \
    X(uint8_t, gs_ui_input_is_mouse_hovering_still_delay_rect, (gs_ui_context c, gs_ui_rect r, float *timer, float delay)) \
    X(uint8_t, gs_ui_input_is_mouse_hovering_still_delay_clicked_rect, (gs_ui_context c, gs_ui_rect r, float *timer, float delay, uint8_t *clicked)) \
    X(uint8_t, gs_ui_input_is_mouse_moved, (gs_ui_context c)) \
    X(uint8_t, gs_ui_input_mouse_clicked, (gs_ui_context c, int64_t button, gs_ui_rect r)) \
    X(uint8_t, gs_ui_input_is_mouse_down, (gs_ui_context c, int64_t button)) \
    X(uint8_t, gs_ui_input_is_mouse_pressed, (gs_ui_context c, int64_t button)) \
    X(uint8_t, gs_ui_input_is_mouse_released, (gs_ui_context c, int64_t button)) \
    X(uint8_t, gs_ui_input_is_key_pressed, (gs_ui_context c, int64_t key)) \
    X(uint8_t, gs_ui_input_is_key_released, (gs_ui_context c, int64_t key)) \
    X(uint8_t, gs_ui_input_is_key_down, (gs_ui_context c, int64_t key)) \
    /* What was drawn: the command list, or triangles. */ \
    X(int64_t, gs_ui_commands, (gs_ui_context c, gs_ui_command_slice out)) \
    X(int64_t, gs_ui_command_points, (gs_ui_context c, gs_ui_int2_slice out)) \
    X(int64_t, gs_ui_command_text, (gs_ui_context c, gs_ui_bytes out)) \
    X(int64_t, gs_ui_convert, (gs_ui_context c, const gs_ui_convert_config *config)) \
    X(int64_t, gs_ui_vertices, (gs_ui_context c, gs_ui_vertex_slice out)) \
    X(int64_t, gs_ui_indices, (gs_ui_context c, gs_ui_u32_slice out)) \
    X(int64_t, gs_ui_draw_commands, (gs_ui_context c, gs_ui_draw_command_slice out)) \
    /* Windows. */ \
    X(uint8_t, gs_ui_begin, (gs_ui_context c, gs_ui_bytes title, gs_ui_rect bounds, int64_t flags)) \
    X(uint8_t, gs_ui_begin_titled, (gs_ui_context c, gs_ui_bytes name, gs_ui_bytes title, gs_ui_rect bounds, int64_t flags)) \
    X(void, gs_ui_end, (gs_ui_context c)) \
    X(uint8_t, gs_ui_window_find, (gs_ui_context c, gs_ui_bytes name)) \
    X(gs_ui_rect, gs_ui_window_get_bounds, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_window_get_position, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_window_get_size, (gs_ui_context c)) \
    X(float, gs_ui_window_get_width, (gs_ui_context c)) \
    X(float, gs_ui_window_get_height, (gs_ui_context c)) \
    X(gs_ui_rect, gs_ui_window_get_content_region, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_window_get_content_region_min, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_window_get_content_region_max, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_window_get_content_region_size, (gs_ui_context c)) \
    X(gs_ui_scroll, gs_ui_window_get_scroll, (gs_ui_context c)) \
    X(uint8_t, gs_ui_window_has_focus, (gs_ui_context c)) \
    X(uint8_t, gs_ui_window_is_hovered, (gs_ui_context c)) \
    X(uint8_t, gs_ui_window_is_collapsed, (gs_ui_context c, gs_ui_bytes name)) \
    X(uint8_t, gs_ui_window_is_closed, (gs_ui_context c, gs_ui_bytes name)) \
    X(uint8_t, gs_ui_window_is_hidden, (gs_ui_context c, gs_ui_bytes name)) \
    X(uint8_t, gs_ui_window_is_active, (gs_ui_context c, gs_ui_bytes name)) \
    X(uint8_t, gs_ui_window_is_any_hovered, (gs_ui_context c)) \
    X(uint8_t, gs_ui_item_is_any_active, (gs_ui_context c)) \
    X(void, gs_ui_window_set_bounds, (gs_ui_context c, gs_ui_bytes name, gs_ui_rect bounds)) \
    X(void, gs_ui_window_set_position, (gs_ui_context c, gs_ui_bytes name, gs_ui_float2 pos)) \
    X(void, gs_ui_window_set_size, (gs_ui_context c, gs_ui_bytes name, gs_ui_float2 size)) \
    X(void, gs_ui_window_set_focus, (gs_ui_context c, gs_ui_bytes name)) \
    X(void, gs_ui_window_set_scroll, (gs_ui_context c, int64_t x, int64_t y)) \
    X(void, gs_ui_window_close, (gs_ui_context c, gs_ui_bytes name)) \
    X(void, gs_ui_window_collapse, (gs_ui_context c, gs_ui_bytes name, int64_t state)) \
    X(void, gs_ui_window_collapse_if, (gs_ui_context c, gs_ui_bytes name, int64_t state, uint8_t cond)) \
    X(void, gs_ui_window_show, (gs_ui_context c, gs_ui_bytes name, int64_t state)) \
    X(void, gs_ui_window_show_if, (gs_ui_context c, gs_ui_bytes name, int64_t state, uint8_t cond)) \
    X(void, gs_ui_rule_horizontal, (gs_ui_context c, gs_ui_color color, uint8_t rounding)) \
    /* Layout. */ \
    X(void, gs_ui_layout_set_min_row_height, (gs_ui_context c, float height)) \
    X(void, gs_ui_layout_reset_min_row_height, (gs_ui_context c)) \
    X(gs_ui_rect, gs_ui_layout_widget_bounds, (gs_ui_context c)) \
    X(float, gs_ui_layout_ratio_from_pixel, (gs_ui_context c, float pixel_width)) \
    X(void, gs_ui_layout_row_dynamic, (gs_ui_context c, float height, int64_t cols)) \
    X(void, gs_ui_layout_row_static, (gs_ui_context c, float height, int64_t item_width, int64_t cols)) \
    X(void, gs_ui_layout_row_begin, (gs_ui_context c, int64_t format, float row_height, int64_t cols)) \
    X(void, gs_ui_layout_row_push, (gs_ui_context c, float value)) \
    X(void, gs_ui_layout_row_end, (gs_ui_context c)) \
    X(void, gs_ui_layout_row, (gs_ui_context c, int64_t format, float height, gs_ui_f32_slice ratios)) \
    X(void, gs_ui_layout_row_template_begin, (gs_ui_context c, float row_height)) \
    X(void, gs_ui_layout_row_template_push_dynamic, (gs_ui_context c)) \
    X(void, gs_ui_layout_row_template_push_variable, (gs_ui_context c, float min_width)) \
    X(void, gs_ui_layout_row_template_push_static, (gs_ui_context c, float width)) \
    X(void, gs_ui_layout_row_template_end, (gs_ui_context c)) \
    X(void, gs_ui_layout_space_begin, (gs_ui_context c, int64_t format, float height, int64_t widget_count)) \
    X(void, gs_ui_layout_space_push, (gs_ui_context c, gs_ui_rect bounds)) \
    X(void, gs_ui_layout_space_end, (gs_ui_context c)) \
    X(gs_ui_rect, gs_ui_layout_space_bounds, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_layout_space_to_screen, (gs_ui_context c, gs_ui_float2 v)) \
    X(gs_ui_float2, gs_ui_layout_space_to_local, (gs_ui_context c, gs_ui_float2 v)) \
    X(gs_ui_rect, gs_ui_layout_space_rect_to_screen, (gs_ui_context c, gs_ui_rect r)) \
    X(gs_ui_rect, gs_ui_layout_space_rect_to_local, (gs_ui_context c, gs_ui_rect r)) \
    X(void, gs_ui_spacer, (gs_ui_context c)) \
    /* Groups. */ \
    X(uint8_t, gs_ui_group_begin, (gs_ui_context c, gs_ui_bytes title, int64_t flags)) \
    X(uint8_t, gs_ui_group_begin_titled, (gs_ui_context c, gs_ui_bytes id, gs_ui_bytes title, int64_t flags)) \
    X(void, gs_ui_group_end, (gs_ui_context c)) \
    X(uint8_t, gs_ui_group_scrolled_begin, (gs_ui_context c, gs_ui_scroll offset, gs_ui_bytes title, int64_t flags)) \
    X(gs_ui_scroll, gs_ui_group_scrolled_end, (gs_ui_context c)) \
    X(gs_ui_scroll, gs_ui_group_get_scroll, (gs_ui_context c, gs_ui_bytes id)) \
    X(void, gs_ui_group_set_scroll, (gs_ui_context c, gs_ui_bytes id, int64_t x, int64_t y)) \
    /* Trees. */ \
    X(uint8_t, gs_ui_tree_push_hashed, (gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t initial_state, gs_ui_bytes hash, int64_t seed)) \
    X(uint8_t, gs_ui_tree_image_push_hashed, (gs_ui_context c, int64_t type, gs_ui_image img, gs_ui_bytes title, int64_t initial_state, gs_ui_bytes hash, int64_t seed)) \
    X(void, gs_ui_tree_pop, (gs_ui_context c)) \
    X(uint8_t, gs_ui_tree_state_push, (gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t *state)) \
    X(uint8_t, gs_ui_tree_state_image_push, (gs_ui_context c, int64_t type, gs_ui_image img, gs_ui_bytes title, int64_t *state)) \
    X(void, gs_ui_tree_state_pop, (gs_ui_context c)) \
    X(uint8_t, gs_ui_tree_element_push_hashed, (gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t initial_state, uint8_t *selected, gs_ui_bytes hash, int64_t seed)) \
    X(uint8_t, gs_ui_tree_element_image_push_hashed, (gs_ui_context c, int64_t type, gs_ui_image img, gs_ui_bytes title, int64_t initial_state, uint8_t *selected, gs_ui_bytes hash, int64_t seed)) \
    X(void, gs_ui_tree_element_pop, (gs_ui_context c)) \
    /* List views. */ \
    X(uint8_t, gs_ui_list_view_begin, (gs_ui_context c, gs_ui_list_view *out, gs_ui_bytes id, int64_t flags, int64_t row_height, int64_t row_count)) \
    X(void, gs_ui_list_view_end, (gs_ui_context c)) \
    /* Widgets in general. */ \
    X(int64_t, gs_ui_widget, (gs_ui_context c, gs_ui_rect *bounds)) \
    X(gs_ui_rect, gs_ui_widget_bounds, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_widget_position, (gs_ui_context c)) \
    X(gs_ui_float2, gs_ui_widget_size, (gs_ui_context c)) \
    X(float, gs_ui_widget_width, (gs_ui_context c)) \
    X(float, gs_ui_widget_height, (gs_ui_context c)) \
    X(uint8_t, gs_ui_widget_is_hovered, (gs_ui_context c)) \
    X(uint8_t, gs_ui_widget_is_mouse_clicked, (gs_ui_context c, int64_t button)) \
    X(uint8_t, gs_ui_widget_has_mouse_click_down, (gs_ui_context c, int64_t button, uint8_t down)) \
    X(void, gs_ui_spacing, (gs_ui_context c, int64_t cols)) \
    X(void, gs_ui_widget_disable_begin, (gs_ui_context c)) \
    X(void, gs_ui_widget_disable_end, (gs_ui_context c)) \
    /* Text and images. */ \
    X(void, gs_ui_label, (gs_ui_context c, gs_ui_bytes text, int64_t align)) \
    X(void, gs_ui_label_colored, (gs_ui_context c, gs_ui_bytes text, int64_t align, gs_ui_color color)) \
    X(void, gs_ui_label_wrap, (gs_ui_context c, gs_ui_bytes text)) \
    X(void, gs_ui_label_colored_wrap, (gs_ui_context c, gs_ui_bytes text, gs_ui_color color)) \
    X(void, gs_ui_image_widget, (gs_ui_context c, gs_ui_image img)) \
    X(void, gs_ui_image_color, (gs_ui_context c, gs_ui_image img, gs_ui_color color)) \
    X(void, gs_ui_value_bool, (gs_ui_context c, gs_ui_bytes prefix, uint8_t value)) \
    X(void, gs_ui_value_int, (gs_ui_context c, gs_ui_bytes prefix, int64_t value)) \
    X(void, gs_ui_value_uint, (gs_ui_context c, gs_ui_bytes prefix, uint64_t value)) \
    X(void, gs_ui_value_float, (gs_ui_context c, gs_ui_bytes prefix, float value)) \
    X(void, gs_ui_value_color_byte, (gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color)) \
    X(void, gs_ui_value_color_float, (gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color)) \
    X(void, gs_ui_value_color_hex, (gs_ui_context c, gs_ui_bytes prefix, gs_ui_color color)) \
    X(uint8_t, gs_ui_link_label, (gs_ui_context c, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_link_label_colored, (gs_ui_context c, gs_ui_bytes text, int64_t align, gs_ui_color color)) \
    X(uint8_t, gs_ui_link_label_underline, (gs_ui_context c, gs_ui_bytes text, int64_t align, int64_t underline)) \
    X(uint8_t, gs_ui_link_label_styled, (gs_ui_context c, const gs_ui_style_link *style, gs_ui_bytes text, int64_t align)) \
    /* Buttons. */ \
    X(uint8_t, gs_ui_button_label, (gs_ui_context c, gs_ui_bytes text)) \
    X(uint8_t, gs_ui_button_color, (gs_ui_context c, gs_ui_color color)) \
    X(uint8_t, gs_ui_button_symbol, (gs_ui_context c, int64_t symbol)) \
    X(uint8_t, gs_ui_button_image, (gs_ui_context c, gs_ui_image img)) \
    X(uint8_t, gs_ui_button_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_button_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_button_label_styled, (gs_ui_context c, const gs_ui_style_button *style, gs_ui_bytes text)) \
    X(uint8_t, gs_ui_button_symbol_styled, (gs_ui_context c, const gs_ui_style_button *style, int64_t symbol)) \
    X(uint8_t, gs_ui_button_image_styled, (gs_ui_context c, const gs_ui_style_button *style, gs_ui_image img)) \
    X(uint8_t, gs_ui_button_symbol_label_styled, (gs_ui_context c, const gs_ui_style_button *style, int64_t symbol, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_button_image_label_styled, (gs_ui_context c, const gs_ui_style_button *style, gs_ui_image img, gs_ui_bytes text, int64_t align)) \
    X(void, gs_ui_button_set_behavior, (gs_ui_context c, int64_t behavior)) \
    X(uint8_t, gs_ui_button_push_behavior, (gs_ui_context c, int64_t behavior)) \
    X(uint8_t, gs_ui_button_pop_behavior, (gs_ui_context c)) \
    /* Checkboxes, radio buttons and selectables. */ \
    X(uint8_t, gs_ui_check_label, (gs_ui_context c, gs_ui_bytes text, uint8_t active)) \
    X(uint8_t, gs_ui_check_label_align, (gs_ui_context c, gs_ui_bytes text, uint8_t active, int64_t widget_align, int64_t text_align)) \
    X(int64_t, gs_ui_check_flags_label, (gs_ui_context c, gs_ui_bytes text, int64_t flags, int64_t value)) \
    X(uint8_t, gs_ui_checkbox_label, (gs_ui_context c, gs_ui_bytes text, uint8_t *active)) \
    X(uint8_t, gs_ui_checkbox_label_align, (gs_ui_context c, gs_ui_bytes text, uint8_t *active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, gs_ui_checkbox_flags_label, (gs_ui_context c, gs_ui_bytes text, int64_t *flags, int64_t value)) \
    X(uint8_t, gs_ui_radio_label, (gs_ui_context c, gs_ui_bytes text, uint8_t *active)) \
    X(uint8_t, gs_ui_radio_label_align, (gs_ui_context c, gs_ui_bytes text, uint8_t *active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, gs_ui_option_label, (gs_ui_context c, gs_ui_bytes text, uint8_t active)) \
    X(uint8_t, gs_ui_option_label_align, (gs_ui_context c, gs_ui_bytes text, uint8_t active, int64_t widget_align, int64_t text_align)) \
    X(uint8_t, gs_ui_selectable_label, (gs_ui_context c, gs_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, gs_ui_selectable_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, gs_ui_selectable_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align, uint8_t *value)) \
    X(uint8_t, gs_ui_select_label, (gs_ui_context c, gs_ui_bytes text, int64_t align, uint8_t value)) \
    X(uint8_t, gs_ui_select_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align, uint8_t value)) \
    X(uint8_t, gs_ui_select_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align, uint8_t value)) \
    /* Sliders, knobs, progress bars and color pickers. */ \
    X(float, gs_ui_slide_float, (gs_ui_context c, float min, float value, float max, float step)) \
    X(int64_t, gs_ui_slide_int, (gs_ui_context c, int64_t min, int64_t value, int64_t max, int64_t step)) \
    X(uint8_t, gs_ui_slider_float, (gs_ui_context c, float min, float *value, float max, float step)) \
    X(uint8_t, gs_ui_slider_int, (gs_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step)) \
    X(uint8_t, gs_ui_knob_float, (gs_ui_context c, float min, float *value, float max, float step, int64_t zero_direction, float dead_zone_degrees)) \
    X(uint8_t, gs_ui_knob_int, (gs_ui_context c, int64_t min, int64_t *value, int64_t max, int64_t step, int64_t zero_direction, float dead_zone_degrees)) \
    X(uint8_t, gs_ui_progress, (gs_ui_context c, int64_t *cur, int64_t max, uint8_t modifiable)) \
    X(int64_t, gs_ui_prog, (gs_ui_context c, int64_t cur, int64_t max, uint8_t modifiable)) \
    X(gs_ui_colorf, gs_ui_color_picker, (gs_ui_context c, gs_ui_colorf color, int64_t format)) \
    X(uint8_t, gs_ui_color_pick, (gs_ui_context c, gs_ui_colorf *color, int64_t format)) \
    /* Properties: a number dragged, stepped or typed. */ \
    X(uint8_t, gs_ui_property_int, (gs_ui_context c, gs_ui_bytes name, int64_t min, int64_t *value, int64_t max, int64_t step, float inc_per_pixel)) \
    X(uint8_t, gs_ui_property_float, (gs_ui_context c, gs_ui_bytes name, float min, float *value, float max, float step, float inc_per_pixel)) \
    X(uint8_t, gs_ui_property_double, (gs_ui_context c, gs_ui_bytes name, double min, double *value, double max, double step, float inc_per_pixel)) \
    X(int64_t, gs_ui_propertyi, (gs_ui_context c, gs_ui_bytes name, int64_t min, int64_t value, int64_t max, int64_t step, float inc_per_pixel)) \
    X(float, gs_ui_propertyf, (gs_ui_context c, gs_ui_bytes name, float min, float value, float max, float step, float inc_per_pixel)) \
    X(double, gs_ui_propertyd, (gs_ui_context c, gs_ui_bytes name, double min, double value, double max, double step, float inc_per_pixel)) \
    /* Text editing. */ \
    X(int64_t, gs_ui_edit_string, (gs_ui_context c, int64_t flags, gs_ui_bytes buffer, int64_t *len, int64_t filter)) \
    X(int64_t, gs_ui_edit_buffer, (gs_ui_context c, int64_t flags, gs_ui_text_edit edit, int64_t filter)) \
    X(void, gs_ui_edit_focus, (gs_ui_context c, int64_t flags)) \
    X(void, gs_ui_edit_unfocus, (gs_ui_context c)) \
    X(uint8_t, gs_ui_filter_accepts, (int64_t filter, uint32_t rune)) \
    X(gs_ui_text_edit, gs_ui_create_text_edit, (void)) \
    X(void, gs_ui_destroy_text_edit, (gs_ui_text_edit e)) \
    X(uint8_t, gs_ui_text_edit_is_valid, (gs_ui_text_edit e)) \
    X(int64_t, gs_ui_text_edit_text, (gs_ui_text_edit e, gs_ui_bytes out)) \
    X(void, gs_ui_text_edit_set_text, (gs_ui_text_edit e, gs_ui_bytes text)) \
    X(gs_ui_text_edit_state, gs_ui_text_edit_state_of, (gs_ui_text_edit e)) \
    X(void, gs_ui_text_edit_set_cursor, (gs_ui_text_edit e, int64_t cursor, int64_t select_start, int64_t select_end)) \
    X(void, gs_ui_text_edit_set_mode, (gs_ui_text_edit e, int64_t mode)) \
    X(void, gs_ui_textedit_text, (gs_ui_text_edit e, gs_ui_bytes text)) \
    X(void, gs_ui_textedit_delete, (gs_ui_text_edit e, int64_t where, int64_t len)) \
    X(void, gs_ui_textedit_delete_selection, (gs_ui_text_edit e)) \
    X(void, gs_ui_textedit_select_all, (gs_ui_text_edit e)) \
    X(uint8_t, gs_ui_textedit_cut, (gs_ui_text_edit e)) \
    X(uint8_t, gs_ui_textedit_paste, (gs_ui_text_edit e, gs_ui_bytes text)) \
    X(void, gs_ui_textedit_undo, (gs_ui_text_edit e)) \
    X(void, gs_ui_textedit_redo, (gs_ui_text_edit e)) \
    /* Charts. */ \
    X(uint8_t, gs_ui_chart_begin, (gs_ui_context c, int64_t type, int64_t count, float min, float max)) \
    X(uint8_t, gs_ui_chart_begin_colored, (gs_ui_context c, int64_t type, gs_ui_color color, gs_ui_color active, int64_t count, float min, float max)) \
    X(void, gs_ui_chart_add_slot, (gs_ui_context c, int64_t type, int64_t count, float min, float max)) \
    X(void, gs_ui_chart_add_slot_colored, (gs_ui_context c, int64_t type, gs_ui_color color, gs_ui_color active, int64_t count, float min, float max)) \
    X(int64_t, gs_ui_chart_push, (gs_ui_context c, float value)) \
    X(int64_t, gs_ui_chart_push_slot, (gs_ui_context c, float value, int64_t slot)) \
    X(void, gs_ui_chart_end, (gs_ui_context c)) \
    X(void, gs_ui_plot, (gs_ui_context c, int64_t type, gs_ui_f32_slice values)) \
    /* Popups. */ \
    X(uint8_t, gs_ui_popup_begin, (gs_ui_context c, int64_t type, gs_ui_bytes title, int64_t flags, gs_ui_rect rect)) \
    X(void, gs_ui_popup_close, (gs_ui_context c)) \
    X(void, gs_ui_popup_end, (gs_ui_context c)) \
    X(gs_ui_scroll, gs_ui_popup_get_scroll, (gs_ui_context c)) \
    X(void, gs_ui_popup_set_scroll, (gs_ui_context c, int64_t x, int64_t y)) \
    /* Combo boxes. */ \
    X(int64_t, gs_ui_combo_separator, (gs_ui_context c, gs_ui_bytes items, int64_t separator, int64_t selected, int64_t count, int64_t item_height, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combobox_separator, (gs_ui_context c, gs_ui_bytes items, int64_t separator, int64_t *selected, int64_t count, int64_t item_height, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_label, (gs_ui_context c, gs_ui_bytes selected, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_color, (gs_ui_context c, gs_ui_color color, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_symbol, (gs_ui_context c, int64_t symbol, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_symbol_label, (gs_ui_context c, gs_ui_bytes selected, int64_t symbol, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_image, (gs_ui_context c, gs_ui_image img, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_begin_image_label, (gs_ui_context c, gs_ui_bytes selected, gs_ui_image img, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_combo_item_label, (gs_ui_context c, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_combo_item_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_combo_item_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align)) \
    X(void, gs_ui_combo_close, (gs_ui_context c)) \
    X(void, gs_ui_combo_end, (gs_ui_context c)) \
    /* Contextual menus. */ \
    X(uint8_t, gs_ui_contextual_begin, (gs_ui_context c, int64_t flags, gs_ui_float2 size, gs_ui_rect trigger_bounds)) \
    X(uint8_t, gs_ui_contextual_item_label, (gs_ui_context c, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_contextual_item_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_contextual_item_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align)) \
    X(void, gs_ui_contextual_close, (gs_ui_context c)) \
    X(void, gs_ui_contextual_end, (gs_ui_context c)) \
    /* Tooltips. */ \
    X(void, gs_ui_tooltip, (gs_ui_context c, gs_ui_bytes text)) \
    X(void, gs_ui_tooltip_offset, (gs_ui_context c, gs_ui_bytes text, int64_t position, gs_ui_float2 offset)) \
    X(uint8_t, gs_ui_tooltip_begin, (gs_ui_context c, float width)) \
    X(uint8_t, gs_ui_tooltip_begin_offset, (gs_ui_context c, float width, int64_t position, gs_ui_float2 offset)) \
    X(void, gs_ui_tooltip_end, (gs_ui_context c)) \
    X(void, gs_ui_do_tooltip, (gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds)) \
    X(void, gs_ui_do_tooltip_delay, (gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds, float *timer)) \
    X(void, gs_ui_do_tooltip_delay_clicked, (gs_ui_context c, gs_ui_bytes text, gs_ui_rect bounds, float *timer, uint8_t *clicked)) \
    /* Menus. */ \
    X(void, gs_ui_menubar_begin, (gs_ui_context c)) \
    X(void, gs_ui_menubar_end, (gs_ui_context c)) \
    X(uint8_t, gs_ui_menu_begin_label, (gs_ui_context c, gs_ui_bytes text, int64_t align, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_menu_begin_image, (gs_ui_context c, gs_ui_bytes id, gs_ui_image img, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_menu_begin_image_label, (gs_ui_context c, gs_ui_bytes text, int64_t align, gs_ui_image img, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_menu_begin_symbol, (gs_ui_context c, gs_ui_bytes id, int64_t symbol, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_menu_begin_symbol_label, (gs_ui_context c, gs_ui_bytes text, int64_t align, int64_t symbol, gs_ui_float2 size)) \
    X(uint8_t, gs_ui_menu_item_label, (gs_ui_context c, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_menu_item_image_label, (gs_ui_context c, gs_ui_image img, gs_ui_bytes text, int64_t align)) \
    X(uint8_t, gs_ui_menu_item_symbol_label, (gs_ui_context c, int64_t symbol, gs_ui_bytes text, int64_t align)) \
    X(void, gs_ui_menu_close, (gs_ui_context c)) \
    X(void, gs_ui_menu_end, (gs_ui_context c)) \
    /* Style. */ \
    X(gs_ui_style, gs_ui_style_of, (gs_ui_context c)) \
    X(void, gs_ui_set_style, (gs_ui_context c, const gs_ui_style *style)) \
    X(uint8_t, gs_ui_push_style, (gs_ui_context c, const gs_ui_style *style)) \
    X(uint8_t, gs_ui_pop_style, (gs_ui_context c)) \
    X(void, gs_ui_style_default, (gs_ui_context c)) \
    X(void, gs_ui_style_from_table, (gs_ui_context c, gs_ui_color_slice table)) \
    X(int64_t, gs_ui_default_color_table, (gs_ui_color_slice out)) \
    X(int64_t, gs_ui_style_get_color_by_name, (int64_t color, gs_ui_bytes out)) \
    X(void, gs_ui_style_set_font, (gs_ui_context c, gs_ui_font font)) \
    X(uint8_t, gs_ui_style_push_font, (gs_ui_context c, gs_ui_font font)) \
    X(uint8_t, gs_ui_style_pop_font, (gs_ui_context c)) \
    X(void, gs_ui_style_load_cursor, (gs_ui_context c, int64_t cursor, gs_ui_cursor image)) \
    X(void, gs_ui_style_load_all_cursors, (gs_ui_context c, gs_ui_cursor_slice cursors)) \
    X(uint8_t, gs_ui_style_set_cursor, (gs_ui_context c, int64_t cursor)) \
    X(void, gs_ui_style_show_cursor, (gs_ui_context c)) \
    X(void, gs_ui_style_hide_cursor, (gs_ui_context c)) \
    /* Colors. */ \
    X(gs_ui_color, gs_ui_rgb, (int64_t r, int64_t g, int64_t b)) \
    X(gs_ui_color, gs_ui_rgba, (int64_t r, int64_t g, int64_t b, int64_t a)) \
    X(gs_ui_color, gs_ui_rgba_u32, (uint32_t rgba)) \
    X(gs_ui_color, gs_ui_rgb_f, (float r, float g, float b)) \
    X(gs_ui_color, gs_ui_rgba_f, (float r, float g, float b, float a)) \
    X(gs_ui_color, gs_ui_rgb_cf, (gs_ui_colorf c)) \
    X(gs_ui_color, gs_ui_rgba_cf, (gs_ui_colorf c)) \
    X(gs_ui_color, gs_ui_rgb_hex, (gs_ui_bytes text)) \
    X(gs_ui_color, gs_ui_rgba_hex, (gs_ui_bytes text)) \
    X(gs_ui_color, gs_ui_rgb_factor, (gs_ui_color c, float factor)) \
    X(gs_ui_color, gs_ui_hsv, (int64_t h, int64_t s, int64_t v)) \
    X(gs_ui_color, gs_ui_hsva, (int64_t h, int64_t s, int64_t v, int64_t a)) \
    X(gs_ui_color, gs_ui_hsv_f, (float h, float s, float v)) \
    X(gs_ui_color, gs_ui_hsva_f, (float h, float s, float v, float a)) \
    X(gs_ui_colorf, gs_ui_hsva_colorf, (float h, float s, float v, float a)) \
    X(gs_ui_float4, gs_ui_colorf_hsva_f, (gs_ui_colorf c)) \
    X(gs_ui_colorf, gs_ui_color_cf, (gs_ui_color c)) \
    X(uint32_t, gs_ui_color_u32, (gs_ui_color c)) \
    X(int64_t, gs_ui_color_hex_rgba, (gs_ui_color c, gs_ui_bytes out)) \
    X(int64_t, gs_ui_color_hex_rgb, (gs_ui_color c, gs_ui_bytes out)) \
    X(gs_ui_float3, gs_ui_color_hsv_f, (gs_ui_color c)) \
    X(gs_ui_float4, gs_ui_color_hsva_f, (gs_ui_color c)) \
    /* Drawing on the window's canvas. */ \
    X(void, gs_ui_stroke_line, (gs_ui_context c, float x0, float y0, float x1, float y1, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_curve, (gs_ui_context c, float ax, float ay, float ctrl0x, float ctrl0y, float ctrl1x, float ctrl1y, float bx, float by, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_rect, (gs_ui_context c, gs_ui_rect r, float rounding, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_circle, (gs_ui_context c, gs_ui_rect r, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_arc, (gs_ui_context c, float cx, float cy, float radius, float a_min, float a_max, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_triangle, (gs_ui_context c, float x0, float y0, float x1, float y1, float x2, float y2, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_polyline, (gs_ui_context c, gs_ui_float2_slice points, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_stroke_polygon, (gs_ui_context c, gs_ui_float2_slice points, float line_thickness, gs_ui_color color)) \
    X(void, gs_ui_fill_rect, (gs_ui_context c, gs_ui_rect r, float rounding, gs_ui_color color)) \
    X(void, gs_ui_fill_rect_multi_color, (gs_ui_context c, gs_ui_rect r, gs_ui_color left, gs_ui_color top, gs_ui_color right, gs_ui_color bottom)) \
    X(void, gs_ui_fill_circle, (gs_ui_context c, gs_ui_rect r, gs_ui_color color)) \
    X(void, gs_ui_fill_arc, (gs_ui_context c, float cx, float cy, float radius, float a_min, float a_max, gs_ui_color color)) \
    X(void, gs_ui_fill_triangle, (gs_ui_context c, float x0, float y0, float x1, float y1, float x2, float y2, gs_ui_color color)) \
    X(void, gs_ui_fill_polygon, (gs_ui_context c, gs_ui_float2_slice points, gs_ui_color color)) \
    X(void, gs_ui_draw_image, (gs_ui_context c, gs_ui_rect r, gs_ui_image img, gs_ui_color color)) \
    X(void, gs_ui_draw_nine_slice, (gs_ui_context c, gs_ui_rect r, gs_ui_nine_slice slice, gs_ui_color color)) \
    X(void, gs_ui_draw_text, (gs_ui_context c, gs_ui_rect r, gs_ui_bytes text, gs_ui_font font, gs_ui_color background, gs_ui_color foreground)) \
    X(void, gs_ui_push_scissor, (gs_ui_context c, gs_ui_rect r)) \
    /* Utilities. */ \
    X(uint32_t, gs_ui_murmur_hash, (gs_ui_bytes key, uint32_t seed)) \
    X(void, gs_ui_triangle_from_direction, (gs_ui_rect r, float pad_x, float pad_y, int64_t direction, gs_ui_float2_slice out)) \
    X(uint8_t, gs_ui_strfilter, (gs_ui_bytes text, gs_ui_bytes regexp)) \
    X(uint8_t, gs_ui_strmatch_fuzzy_text, (gs_ui_bytes text, gs_ui_bytes pattern, int64_t *score))

#define GS_UI_PROTO(ret, name, params) ret name params;
GS_UI_API(GS_UI_PROTO)
#undef GS_UI_PROTO

/* The constants stdlib/ui.goose declares, with their Goose types; here they
   are GS_UI_<name>. Each has the value of the Nuklear constant of the same
   name with NK_ in front, except where a comment says otherwise. */
#define GS_UI_CONSTANTS(X) \
    /* Window flags. */ \
    X(i32, WINDOW_BORDER, 1) \
    X(i32, WINDOW_MOVABLE, 2) \
    X(i32, WINDOW_SCALABLE, 4) \
    X(i32, WINDOW_CLOSABLE, 8) \
    X(i32, WINDOW_MINIMIZABLE, 16) \
    X(i32, WINDOW_NO_SCROLLBAR, 32) \
    X(i32, WINDOW_TITLE, 64) \
    X(i32, WINDOW_SCROLL_AUTO_HIDE, 128) \
    X(i32, WINDOW_BACKGROUND, 256) \
    X(i32, WINDOW_SCALE_LEFT, 512) \
    X(i32, WINDOW_NO_INPUT, 1024) \
    /* Text alignment. */ \
    X(i32, TEXT_ALIGN_LEFT, 1) \
    X(i32, TEXT_ALIGN_CENTERED, 2) \
    X(i32, TEXT_ALIGN_RIGHT, 4) \
    X(i32, TEXT_ALIGN_TOP, 8) \
    X(i32, TEXT_ALIGN_MIDDLE, 16) \
    X(i32, TEXT_ALIGN_BOTTOM, 32) \
    X(i32, TEXT_LEFT, 17) \
    X(i32, TEXT_CENTERED, 18) \
    X(i32, TEXT_RIGHT, 20) \
    /* Widget alignment, for the _align toggles. */ \
    X(i32, WIDGET_ALIGN_LEFT, 1) \
    X(i32, WIDGET_ALIGN_CENTERED, 2) \
    X(i32, WIDGET_ALIGN_RIGHT, 4) \
    X(i32, WIDGET_ALIGN_TOP, 8) \
    X(i32, WIDGET_ALIGN_MIDDLE, 16) \
    X(i32, WIDGET_ALIGN_BOTTOM, 32) \
    X(i32, WIDGET_LEFT, 17) \
    X(i32, WIDGET_CENTERED, 18) \
    X(i32, WIDGET_RIGHT, 20) \
    /* What ui::widget found room for (nk_widget_layout_states). */ \
    X(i32, WIDGET_INVALID, 0) \
    X(i32, WIDGET_VALID, 1) \
    X(i32, WIDGET_ROM, 2) \
    X(i32, WIDGET_DISABLED, 3) \
    /* How a widget was used (nk_widget_states), in edit and chart results. */ \
    X(i32, WIDGET_STATE_MODIFIED, 2) \
    X(i32, WIDGET_STATE_INACTIVE, 4) \
    X(i32, WIDGET_STATE_ENTERED, 8) \
    X(i32, WIDGET_STATE_HOVER, 16) \
    X(i32, WIDGET_STATE_ACTIVED, 32) \
    X(i32, WIDGET_STATE_LEFT, 64) \
    X(i32, WIDGET_STATE_HOVERED, 18) \
    X(i32, WIDGET_STATE_ACTIVE, 34) \
    /* Symbols. */ \
    X(i32, SYMBOL_NONE, 0) \
    X(i32, SYMBOL_X, 1) \
    X(i32, SYMBOL_UNDERSCORE, 2) \
    X(i32, SYMBOL_CIRCLE_SOLID, 3) \
    X(i32, SYMBOL_CIRCLE_OUTLINE, 4) \
    X(i32, SYMBOL_RECT_SOLID, 5) \
    X(i32, SYMBOL_RECT_OUTLINE, 6) \
    X(i32, SYMBOL_TRIANGLE_UP, 7) \
    X(i32, SYMBOL_TRIANGLE_DOWN, 8) \
    X(i32, SYMBOL_TRIANGLE_LEFT, 9) \
    X(i32, SYMBOL_TRIANGLE_RIGHT, 10) \
    X(i32, SYMBOL_PLUS, 11) \
    X(i32, SYMBOL_MINUS, 12) \
    X(i32, SYMBOL_TRIANGLE_UP_OUTLINE, 13) \
    X(i32, SYMBOL_TRIANGLE_DOWN_OUTLINE, 14) \
    X(i32, SYMBOL_TRIANGLE_LEFT_OUTLINE, 15) \
    X(i32, SYMBOL_TRIANGLE_RIGHT_OUTLINE, 16) \
    X(i32, SYMBOL_CHEVRON_UP, 17) \
    X(i32, SYMBOL_CHEVRON_RIGHT, 18) \
    X(i32, SYMBOL_CHEVRON_DOWN, 19) \
    X(i32, SYMBOL_CHEVRON_LEFT, 20) \
    X(i32, SYMBOL_HAMBURGER, 21) \
    /* Keys. */ \
    X(i32, KEY_NONE, 0) \
    X(i32, KEY_SHIFT, 1) \
    X(i32, KEY_CTRL, 2) \
    X(i32, KEY_DEL, 3) \
    X(i32, KEY_ENTER, 4) \
    X(i32, KEY_TAB, 5) \
    X(i32, KEY_BACKSPACE, 6) \
    X(i32, KEY_COPY, 7) \
    X(i32, KEY_CUT, 8) \
    X(i32, KEY_PASTE, 9) \
    X(i32, KEY_UP, 10) \
    X(i32, KEY_DOWN, 11) \
    X(i32, KEY_LEFT, 12) \
    X(i32, KEY_RIGHT, 13) \
    X(i32, KEY_TEXT_INSERT_MODE, 14) \
    X(i32, KEY_TEXT_REPLACE_MODE, 15) \
    X(i32, KEY_TEXT_RESET_MODE, 16) \
    X(i32, KEY_TEXT_LINE_START, 17) \
    X(i32, KEY_TEXT_LINE_END, 18) \
    X(i32, KEY_TEXT_START, 19) \
    X(i32, KEY_TEXT_END, 20) \
    X(i32, KEY_TEXT_UNDO, 21) \
    X(i32, KEY_TEXT_REDO, 22) \
    X(i32, KEY_TEXT_SELECT_ALL, 23) \
    X(i32, KEY_TEXT_WORD_LEFT, 24) \
    X(i32, KEY_TEXT_WORD_RIGHT, 25) \
    X(i32, KEY_SCROLL_START, 26) \
    X(i32, KEY_SCROLL_END, 27) \
    X(i32, KEY_SCROLL_DOWN, 28) \
    X(i32, KEY_SCROLL_UP, 29) \
    /* Mouse buttons. */ \
    X(i32, BUTTON_LEFT, 0) \
    X(i32, BUTTON_MIDDLE, 1) \
    X(i32, BUTTON_RIGHT, 2) \
    X(i32, BUTTON_DOUBLE, 3) \
    X(i32, BUTTON_X1, 4) \
    X(i32, BUTTON_X2, 5) \
    /* Button behavior. */ \
    X(i32, BUTTON_DEFAULT, 0) \
    X(i32, BUTTON_REPEATER, 1) \
    /* Directions (nk_heading). */ \
    X(i32, UP, 0) \
    X(i32, RIGHT, 1) \
    X(i32, DOWN, 2) \
    X(i32, LEFT, 3) \
    /* Progress bars. */ \
    X(i32, FIXED, 0) \
    X(i32, MODIFIABLE, 1) \
    /* Orientation. */ \
    X(i32, VERTICAL, 0) \
    X(i32, HORIZONTAL, 1) \
    /* Collapse and show states. */ \
    X(i32, MINIMIZED, 0) \
    X(i32, MAXIMIZED, 1) \
    X(i32, HIDDEN, 0) \
    X(i32, SHOWN, 1) \
    /* Charts, and what chart_push says happened. */ \
    X(i32, CHART_LINES, 0) \
    X(i32, CHART_COLUMN, 1) \
    X(i32, CHART_HOVERING, 1) \
    X(i32, CHART_CLICKED, 2) \
    /* Color formats. */ \
    X(i32, RGB, 0) \
    X(i32, RGBA, 1) \
    /* Popups, layouts, trees. */ \
    X(i32, POPUP_STATIC, 0) \
    X(i32, POPUP_DYNAMIC, 1) \
    X(i32, DYNAMIC, 0) \
    X(i32, STATIC, 1) \
    X(i32, TREE_NODE, 0) \
    X(i32, TREE_TAB, 1) \
    /* Where a tooltip sits (nk_tooltip_pos). */ \
    X(i32, TOP_LEFT, 0) \
    X(i32, TOP_CENTER, 1) \
    X(i32, TOP_RIGHT, 2) \
    X(i32, MIDDLE_LEFT, 3) \
    X(i32, MIDDLE_CENTER, 4) \
    X(i32, MIDDLE_RIGHT, 5) \
    X(i32, BOTTOM_LEFT, 6) \
    X(i32, BOTTOM_CENTER, 7) \
    X(i32, BOTTOM_RIGHT, 8) \
    /* Links. */ \
    X(i32, LINK_UNDERLINE_NONE, 0) \
    X(i32, LINK_UNDERLINE_HOVER, 1) \
    X(i32, LINK_UNDERLINE_ALWAYS, 2) \
    /* Text edits: flags, the usual combinations, and what edit_* returns. */ \
    X(i32, EDIT_DEFAULT, 0) \
    X(i32, EDIT_READ_ONLY, 1) \
    X(i32, EDIT_AUTO_SELECT, 2) \
    X(i32, EDIT_SIG_ENTER, 4) \
    X(i32, EDIT_ALLOW_TAB, 8) \
    X(i32, EDIT_NO_CURSOR, 16) \
    X(i32, EDIT_SELECTABLE, 32) \
    X(i32, EDIT_CLIPBOARD, 64) \
    X(i32, EDIT_CTRL_ENTER_NEWLINE, 128) \
    X(i32, EDIT_NO_HORIZONTAL_SCROLL, 256) \
    X(i32, EDIT_ALWAYS_INSERT_MODE, 512) \
    X(i32, EDIT_MULTILINE, 1024) \
    X(i32, EDIT_GOTO_END_ON_ACTIVATE, 2048) \
    X(i32, EDIT_SIMPLE, 512) \
    X(i32, EDIT_FIELD, 608) \
    X(i32, EDIT_BOX, 1640) \
    X(i32, EDIT_EDITOR, 1128) \
    X(i32, EDIT_ACTIVE, 1) \
    X(i32, EDIT_INACTIVE, 2) \
    X(i32, EDIT_ACTIVATED, 4) \
    X(i32, EDIT_DEACTIVATED, 8) \
    X(i32, EDIT_COMMITTED, 16) \
    /* What a text edit accepts: Nuklear's nk_filter_* (not Nuklear values). */ \
    X(i32, FILTER_DEFAULT, 0) \
    X(i32, FILTER_ASCII, 1) \
    X(i32, FILTER_FLOAT, 2) \
    X(i32, FILTER_DECIMAL, 3) \
    X(i32, FILTER_HEX, 4) \
    X(i32, FILTER_OCT, 5) \
    X(i32, FILTER_BINARY, 6) \
    /* A text editor's mode. */ \
    X(i32, TEXT_EDIT_MODE_VIEW, 0) \
    X(i32, TEXT_EDIT_MODE_INSERT, 1) \
    X(i32, TEXT_EDIT_MODE_REPLACE, 2) \
    /* Style colors, the indices of a color table. */ \
    X(i32, COLOR_TEXT, 0) \
    X(i32, COLOR_WINDOW, 1) \
    X(i32, COLOR_HEADER, 2) \
    X(i32, COLOR_BORDER, 3) \
    X(i32, COLOR_BUTTON, 4) \
    X(i32, COLOR_BUTTON_HOVER, 5) \
    X(i32, COLOR_BUTTON_ACTIVE, 6) \
    X(i32, COLOR_TOGGLE, 7) \
    X(i32, COLOR_TOGGLE_HOVER, 8) \
    X(i32, COLOR_TOGGLE_CURSOR, 9) \
    X(i32, COLOR_SELECT, 10) \
    X(i32, COLOR_SELECT_ACTIVE, 11) \
    X(i32, COLOR_SLIDER, 12) \
    X(i32, COLOR_SLIDER_CURSOR, 13) \
    X(i32, COLOR_SLIDER_CURSOR_HOVER, 14) \
    X(i32, COLOR_SLIDER_CURSOR_ACTIVE, 15) \
    X(i32, COLOR_PROPERTY, 16) \
    X(i32, COLOR_EDIT, 17) \
    X(i32, COLOR_EDIT_CURSOR, 18) \
    X(i32, COLOR_COMBO, 19) \
    X(i32, COLOR_CHART, 20) \
    X(i32, COLOR_CHART_COLOR, 21) \
    X(i32, COLOR_CHART_COLOR_HIGHLIGHT, 22) \
    X(i32, COLOR_SCROLLBAR, 23) \
    X(i32, COLOR_SCROLLBAR_CURSOR, 24) \
    X(i32, COLOR_SCROLLBAR_CURSOR_HOVER, 25) \
    X(i32, COLOR_SCROLLBAR_CURSOR_ACTIVE, 26) \
    X(i32, COLOR_TAB_HEADER, 27) \
    X(i32, COLOR_KNOB, 28) \
    X(i32, COLOR_KNOB_CURSOR, 29) \
    X(i32, COLOR_KNOB_CURSOR_HOVER, 30) \
    X(i32, COLOR_KNOB_CURSOR_ACTIVE, 31) \
    X(i32, COLOR_COUNT, 32) \
    /* Cursors. */ \
    X(i32, CURSOR_ARROW, 0) \
    X(i32, CURSOR_TEXT, 1) \
    X(i32, CURSOR_MOVE, 2) \
    X(i32, CURSOR_RESIZE_VERTICAL, 3) \
    X(i32, CURSOR_RESIZE_HORIZONTAL, 4) \
    X(i32, CURSOR_RESIZE_TOP_LEFT_DOWN_RIGHT, 5) \
    X(i32, CURSOR_RESIZE_TOP_RIGHT_DOWN_LEFT, 6) \
    X(i32, CURSOR_COUNT, 7) \
    /* Style items and window header alignment. */ \
    X(i32, STYLE_ITEM_COLOR, 0) \
    X(i32, STYLE_ITEM_IMAGE, 1) \
    X(i32, STYLE_ITEM_NINE_SLICE, 2) \
    X(i32, HEADER_LEFT, 0) \
    X(i32, HEADER_RIGHT, 1) \
    /* Command kinds (nk_command_type). */ \
    X(i32, COMMAND_NOP, 0) \
    X(i32, COMMAND_SCISSOR, 1) \
    X(i32, COMMAND_LINE, 2) \
    X(i32, COMMAND_CURVE, 3) \
    X(i32, COMMAND_RECT, 4) \
    X(i32, COMMAND_RECT_FILLED, 5) \
    X(i32, COMMAND_RECT_MULTI_COLOR, 6) \
    X(i32, COMMAND_CIRCLE, 7) \
    X(i32, COMMAND_CIRCLE_FILLED, 8) \
    X(i32, COMMAND_ARC, 9) \
    X(i32, COMMAND_ARC_FILLED, 10) \
    X(i32, COMMAND_TRIANGLE, 11) \
    X(i32, COMMAND_TRIANGLE_FILLED, 12) \
    X(i32, COMMAND_POLYGON, 13) \
    X(i32, COMMAND_POLYGON_FILLED, 14) \
    X(i32, COMMAND_POLYLINE, 15) \
    X(i32, COMMAND_TEXT, 16) \
    X(i32, COMMAND_IMAGE, 17) \
    /* What convert returns: 0, or which buffer ran out (nk_convert_result). */ \
    X(i32, CONVERT_SUCCESS, 0) \
    X(i32, CONVERT_INVALID_PARAM, 1) \
    X(i32, CONVERT_COMMAND_BUFFER_FULL, 2) \
    X(i32, CONVERT_VERTEX_BUFFER_FULL, 4) \
    X(i32, CONVERT_ELEMENT_BUFFER_FULL, 8) \
    /* The characters a font is baked with (not Nuklear values). */ \
    X(i32, RANGE_DEFAULT, 0) \
    X(i32, RANGE_CHINESE, 1) \
    X(i32, RANGE_CYRILLIC, 2) \
    X(i32, RANGE_KOREAN, 3)

#define GS_UI_ENUM(type, name, value) GS_UI_##name = value,
enum { GS_UI_CONSTANTS(GS_UI_ENUM) };
#undef GS_UI_ENUM

#ifdef __cplusplus
}
#endif

#endif
