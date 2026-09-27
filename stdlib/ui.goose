// The Goose standard library: `ui` (docs/stdlib.md), immediate-mode user
// interfaces on Nuklear: windows, layout, groups, trees, buttons, check
// boxes, radio buttons, sliders, knobs, progress bars, properties, text
// editing, combo boxes, menus, popups, tooltips, charts, color pickers,
// styles and fonts. The functions are C, the layer in src/ui/ over Nuklear,
// declared here as extern fns against the list in src/ui/ui_api.h; the test
// suite checks the two agree.
//
// The API follows Nuklear's closely, under Goose names: nk_button_label is
// ui::button_label, and a Nuklear program reads almost line for line as a
// Goose one. A context handle takes the place of Nuklear's nk_context*.
// Strings are Goose strings, so where Nuklear has a `_label` function for C
// strings and a `_text` one taking a length, there is only the `_label`
// one. A value a widget changes, which Nuklear takes a pointer to (a
// check box's bool, a slider's float), is a reference here, changed during
// the call.
//
// Immediate mode: every frame the program says which windows and widgets
// there are, and a widget's call answers what the user did with it last
// frame (a button returns true when clicked). A frame goes:
//
//   ui::input_from_gfx(ctx);        // or input_begin, input_* and input_end
//   if ui::begin(ctx, "Window", ui::rect(10, 10, 200, 150), ui::WINDOW_TITLE) {
//       ui::layout_row_dynamic(ctx, 30.0, 1);
//       if ui::button_label(ctx, "Press me") { ... }
//   }
//   ui::end(ctx);                   // whether begin opened the window or not
//   ui::render(ctx);                // draws it over the gfx screen
//
// The layer itself has no window or GPU: render() and input_from_gfx()
// below draw through the gfx module and read its input, and what Nuklear
// draws can also be had as a command list or as triangles for any renderer.
//
// Errors: a call that can fail for reasons outside the program (a font file
// that is not there) returns false or a zero handle, and error() says why.
// A call the program should not have made -- a widget outside a window, an
// end that ends nothing, a released handle, a window begun twice in a
// frame -- is reported as it happens and aborts the program at the next
// frame's input, render(), destroy() or check().

namespace ui;

import std;
import vec;
import math;
import gfx;

// --- constants: GS_UI_CONSTANTS in src/ui/ui_api.h ---------------------------

// Window flags, combined with |; groups and popups take them too.
let WINDOW_BORDER: i32 = 1;
let WINDOW_MOVABLE: i32 = 2;
let WINDOW_SCALABLE: i32 = 4;
let WINDOW_CLOSABLE: i32 = 8;
let WINDOW_MINIMIZABLE: i32 = 16;
let WINDOW_NO_SCROLLBAR: i32 = 32;
let WINDOW_TITLE: i32 = 64;
let WINDOW_SCROLL_AUTO_HIDE: i32 = 128;
let WINDOW_BACKGROUND: i32 = 256;
let WINDOW_SCALE_LEFT: i32 = 512;
let WINDOW_NO_INPUT: i32 = 1024;

// Text alignment: the ALIGN_ parts combine, TEXT_LEFT, _CENTERED and _RIGHT
// are the usual combinations, centered vertically.
let TEXT_ALIGN_LEFT: i32 = 1;
let TEXT_ALIGN_CENTERED: i32 = 2;
let TEXT_ALIGN_RIGHT: i32 = 4;
let TEXT_ALIGN_TOP: i32 = 8;
let TEXT_ALIGN_MIDDLE: i32 = 16;
let TEXT_ALIGN_BOTTOM: i32 = 32;
let TEXT_LEFT: i32 = 17;
let TEXT_CENTERED: i32 = 18;
let TEXT_RIGHT: i32 = 20;

// Where the box of a check box, radio button or option sits, for the
// _align ones.
let WIDGET_ALIGN_LEFT: i32 = 1;
let WIDGET_ALIGN_CENTERED: i32 = 2;
let WIDGET_ALIGN_RIGHT: i32 = 4;
let WIDGET_ALIGN_TOP: i32 = 8;
let WIDGET_ALIGN_MIDDLE: i32 = 16;
let WIDGET_ALIGN_BOTTOM: i32 = 32;
let WIDGET_LEFT: i32 = 17;
let WIDGET_CENTERED: i32 = 18;
let WIDGET_RIGHT: i32 = 20;

// What widget() found room for.
let WIDGET_INVALID: i32 = 0;      // out of view: neither drawn nor updated
let WIDGET_VALID: i32 = 1;        // in view, and the mouse over the window
let WIDGET_ROM: i32 = 2;          // in view, not taking input
let WIDGET_DISABLED: i32 = 3;     // between widget_disable_begin and _end

// How a widget was used, in what edit_* and chart_push return.
let WIDGET_STATE_MODIFIED: i32 = 2;
let WIDGET_STATE_INACTIVE: i32 = 4;
let WIDGET_STATE_ENTERED: i32 = 8;
let WIDGET_STATE_HOVER: i32 = 16;
let WIDGET_STATE_ACTIVED: i32 = 32;
let WIDGET_STATE_LEFT: i32 = 64;
let WIDGET_STATE_HOVERED: i32 = 18;
let WIDGET_STATE_ACTIVE: i32 = 34;

// Symbols, drawn on buttons, combo boxes and menus.
let SYMBOL_NONE: i32 = 0;
let SYMBOL_X: i32 = 1;
let SYMBOL_UNDERSCORE: i32 = 2;
let SYMBOL_CIRCLE_SOLID: i32 = 3;
let SYMBOL_CIRCLE_OUTLINE: i32 = 4;
let SYMBOL_RECT_SOLID: i32 = 5;
let SYMBOL_RECT_OUTLINE: i32 = 6;
let SYMBOL_TRIANGLE_UP: i32 = 7;
let SYMBOL_TRIANGLE_DOWN: i32 = 8;
let SYMBOL_TRIANGLE_LEFT: i32 = 9;
let SYMBOL_TRIANGLE_RIGHT: i32 = 10;
let SYMBOL_PLUS: i32 = 11;
let SYMBOL_MINUS: i32 = 12;
let SYMBOL_TRIANGLE_UP_OUTLINE: i32 = 13;
let SYMBOL_TRIANGLE_DOWN_OUTLINE: i32 = 14;
let SYMBOL_TRIANGLE_LEFT_OUTLINE: i32 = 15;
let SYMBOL_TRIANGLE_RIGHT_OUTLINE: i32 = 16;
let SYMBOL_CHEVRON_UP: i32 = 17;
let SYMBOL_CHEVRON_RIGHT: i32 = 18;
let SYMBOL_CHEVRON_DOWN: i32 = 19;
let SYMBOL_CHEVRON_LEFT: i32 = 20;
let SYMBOL_HAMBURGER: i32 = 21;

// Keys, as input_key takes them: editing keys, and the shortcuts a text
// field acts on.
let KEY_NONE: i32 = 0;
let KEY_SHIFT: i32 = 1;
let KEY_CTRL: i32 = 2;
let KEY_DEL: i32 = 3;
let KEY_ENTER: i32 = 4;
let KEY_TAB: i32 = 5;
let KEY_BACKSPACE: i32 = 6;
let KEY_COPY: i32 = 7;
let KEY_CUT: i32 = 8;
let KEY_PASTE: i32 = 9;
let KEY_UP: i32 = 10;
let KEY_DOWN: i32 = 11;
let KEY_LEFT: i32 = 12;
let KEY_RIGHT: i32 = 13;
let KEY_TEXT_INSERT_MODE: i32 = 14;
let KEY_TEXT_REPLACE_MODE: i32 = 15;
let KEY_TEXT_RESET_MODE: i32 = 16;
let KEY_TEXT_LINE_START: i32 = 17;
let KEY_TEXT_LINE_END: i32 = 18;
let KEY_TEXT_START: i32 = 19;
let KEY_TEXT_END: i32 = 20;
let KEY_TEXT_UNDO: i32 = 21;
let KEY_TEXT_REDO: i32 = 22;
let KEY_TEXT_SELECT_ALL: i32 = 23;
let KEY_TEXT_WORD_LEFT: i32 = 24;
let KEY_TEXT_WORD_RIGHT: i32 = 25;
let KEY_SCROLL_START: i32 = 26;
let KEY_SCROLL_END: i32 = 27;
let KEY_SCROLL_DOWN: i32 = 28;
let KEY_SCROLL_UP: i32 = 29;

// Mouse buttons, and what a button does while held down.
let BUTTON_LEFT: i32 = 0;
let BUTTON_MIDDLE: i32 = 1;
let BUTTON_RIGHT: i32 = 2;
let BUTTON_DOUBLE: i32 = 3;       // the left button, clicked twice
let BUTTON_X1: i32 = 4;
let BUTTON_X2: i32 = 5;
let BUTTON_DEFAULT: i32 = 0;      // a click
let BUTTON_REPEATER: i32 = 1;     // every frame it is held

// Directions, for a knob's zero and triangle_from_direction.
let UP: i32 = 0;
let RIGHT: i32 = 1;
let DOWN: i32 = 2;
let LEFT: i32 = 3;

// A progress bar the user can drag, or not; an orientation.
let FIXED: i32 = 0;
let MODIFIABLE: i32 = 1;
let VERTICAL: i32 = 0;
let HORIZONTAL: i32 = 1;

// Tree nodes and windows, open or closed; windows shown or not.
let MINIMIZED: i32 = 0;
let MAXIMIZED: i32 = 1;
let HIDDEN: i32 = 0;
let SHOWN: i32 = 1;

// Charts, and what chart_push says the mouse did to a value.
let CHART_LINES: i32 = 0;
let CHART_COLUMN: i32 = 1;
let CHART_HOVERING: i32 = 1;
let CHART_CLICKED: i32 = 2;

// What a color picker edits.
let RGB: i32 = 0;
let RGBA: i32 = 1;

// Popups, rows and trees.
let POPUP_STATIC: i32 = 0;
let POPUP_DYNAMIC: i32 = 1;       // as high as what is in it
let DYNAMIC: i32 = 0;             // widths as parts of the row, from 0 to 1
let STATIC: i32 = 1;              // widths in pixels
let TREE_NODE: i32 = 0;
let TREE_TAB: i32 = 1;

// Where a tooltip sits against the pointer.
let TOP_LEFT: i32 = 0;
let TOP_CENTER: i32 = 1;
let TOP_RIGHT: i32 = 2;
let MIDDLE_LEFT: i32 = 3;
let MIDDLE_CENTER: i32 = 4;
let MIDDLE_RIGHT: i32 = 5;
let BOTTOM_LEFT: i32 = 6;
let BOTTOM_CENTER: i32 = 7;
let BOTTOM_RIGHT: i32 = 8;

// When a link is underlined.
let LINK_UNDERLINE_NONE: i32 = 0;
let LINK_UNDERLINE_HOVER: i32 = 1;
let LINK_UNDERLINE_ALWAYS: i32 = 2;

// Text editing: flags, the usual combinations (EDIT_FIELD for one line,
// EDIT_BOX for several), and what edit_* returns.
let EDIT_DEFAULT: i32 = 0;
let EDIT_READ_ONLY: i32 = 1;
let EDIT_AUTO_SELECT: i32 = 2;
let EDIT_SIG_ENTER: i32 = 4;
let EDIT_ALLOW_TAB: i32 = 8;
let EDIT_NO_CURSOR: i32 = 16;
let EDIT_SELECTABLE: i32 = 32;
let EDIT_CLIPBOARD: i32 = 64;
let EDIT_CTRL_ENTER_NEWLINE: i32 = 128;
let EDIT_NO_HORIZONTAL_SCROLL: i32 = 256;
let EDIT_ALWAYS_INSERT_MODE: i32 = 512;
let EDIT_MULTILINE: i32 = 1024;
let EDIT_GOTO_END_ON_ACTIVATE: i32 = 2048;
let EDIT_SIMPLE: i32 = 512;
let EDIT_FIELD: i32 = 608;
let EDIT_BOX: i32 = 1640;
let EDIT_EDITOR: i32 = 1128;
let EDIT_ACTIVE: i32 = 1;         // being edited
let EDIT_INACTIVE: i32 = 2;
let EDIT_ACTIVATED: i32 = 4;      // this frame
let EDIT_DEACTIVATED: i32 = 8;    // this frame
let EDIT_COMMITTED: i32 = 16;     // Enter pressed, with EDIT_SIG_ENTER

// What a text edit accepts: Nuklear's filters.
let FILTER_DEFAULT: i32 = 0;      // anything
let FILTER_ASCII: i32 = 1;
let FILTER_FLOAT: i32 = 2;
let FILTER_DECIMAL: i32 = 3;
let FILTER_HEX: i32 = 4;
let FILTER_OCT: i32 = 5;
let FILTER_BINARY: i32 = 6;

// A text editor's mode.
let TEXT_EDIT_MODE_VIEW: i32 = 0;
let TEXT_EDIT_MODE_INSERT: i32 = 1;
let TEXT_EDIT_MODE_REPLACE: i32 = 2;

// The colors of a style table (style_from_table), by what they color.
let COLOR_TEXT: i32 = 0;
let COLOR_WINDOW: i32 = 1;
let COLOR_HEADER: i32 = 2;
let COLOR_BORDER: i32 = 3;
let COLOR_BUTTON: i32 = 4;
let COLOR_BUTTON_HOVER: i32 = 5;
let COLOR_BUTTON_ACTIVE: i32 = 6;
let COLOR_TOGGLE: i32 = 7;
let COLOR_TOGGLE_HOVER: i32 = 8;
let COLOR_TOGGLE_CURSOR: i32 = 9;
let COLOR_SELECT: i32 = 10;
let COLOR_SELECT_ACTIVE: i32 = 11;
let COLOR_SLIDER: i32 = 12;
let COLOR_SLIDER_CURSOR: i32 = 13;
let COLOR_SLIDER_CURSOR_HOVER: i32 = 14;
let COLOR_SLIDER_CURSOR_ACTIVE: i32 = 15;
let COLOR_PROPERTY: i32 = 16;
let COLOR_EDIT: i32 = 17;
let COLOR_EDIT_CURSOR: i32 = 18;
let COLOR_COMBO: i32 = 19;
let COLOR_CHART: i32 = 20;
let COLOR_CHART_COLOR: i32 = 21;
let COLOR_CHART_COLOR_HIGHLIGHT: i32 = 22;
let COLOR_SCROLLBAR: i32 = 23;
let COLOR_SCROLLBAR_CURSOR: i32 = 24;
let COLOR_SCROLLBAR_CURSOR_HOVER: i32 = 25;
let COLOR_SCROLLBAR_CURSOR_ACTIVE: i32 = 26;
let COLOR_TAB_HEADER: i32 = 27;
let COLOR_KNOB: i32 = 28;
let COLOR_KNOB_CURSOR: i32 = 29;
let COLOR_KNOB_CURSOR_HOVER: i32 = 30;
let COLOR_KNOB_CURSOR_ACTIVE: i32 = 31;
let COLOR_COUNT: i32 = 32;

// Software cursors, which Nuklear draws itself once they are loaded.
let CURSOR_ARROW: i32 = 0;
let CURSOR_TEXT: i32 = 1;
let CURSOR_MOVE: i32 = 2;
let CURSOR_RESIZE_VERTICAL: i32 = 3;
let CURSOR_RESIZE_HORIZONTAL: i32 = 4;
let CURSOR_RESIZE_TOP_LEFT_DOWN_RIGHT: i32 = 5;
let CURSOR_RESIZE_TOP_RIGHT_DOWN_LEFT: i32 = 6;
let CURSOR_COUNT: i32 = 7;

// What a style item is, and where a window's title sits.
let STYLE_ITEM_COLOR: i32 = 0;
let STYLE_ITEM_IMAGE: i32 = 1;
let STYLE_ITEM_NINE_SLICE: i32 = 2;
let HEADER_LEFT: i32 = 0;
let HEADER_RIGHT: i32 = 1;

// The kinds of commands() Nuklear draws with.
let COMMAND_NOP: i32 = 0;
let COMMAND_SCISSOR: i32 = 1;
let COMMAND_LINE: i32 = 2;
let COMMAND_CURVE: i32 = 3;
let COMMAND_RECT: i32 = 4;
let COMMAND_RECT_FILLED: i32 = 5;
let COMMAND_RECT_MULTI_COLOR: i32 = 6;
let COMMAND_CIRCLE: i32 = 7;
let COMMAND_CIRCLE_FILLED: i32 = 8;
let COMMAND_ARC: i32 = 9;
let COMMAND_ARC_FILLED: i32 = 10;
let COMMAND_TRIANGLE: i32 = 11;
let COMMAND_TRIANGLE_FILLED: i32 = 12;
let COMMAND_POLYGON: i32 = 13;
let COMMAND_POLYGON_FILLED: i32 = 14;
let COMMAND_POLYLINE: i32 = 15;
let COMMAND_TEXT: i32 = 16;
let COMMAND_IMAGE: i32 = 17;

// What convert() returns: 0, or what went wrong.
let CONVERT_SUCCESS: i32 = 0;
let CONVERT_INVALID_PARAM: i32 = 1;
let CONVERT_COMMAND_BUFFER_FULL: i32 = 2;
let CONVERT_VERTEX_BUFFER_FULL: i32 = 4;
let CONVERT_ELEMENT_BUFFER_FULL: i32 = 8;

// The characters a font is baked with.
let RANGE_DEFAULT: i32 = 0;       // Latin-1
let RANGE_CHINESE: i32 = 1;
let RANGE_CYRILLIC: i32 = 2;
let RANGE_KOREAN: i32 = 3;

// --- handles and values: the structs of src/ui/ui_api.h ----------------------

// A Nuklear object, by handle: a destroyed one is an error to use, not a
// crash. A zero handle is none.
struct Context { id: u32 }
struct FontAtlas { id: u32 }
struct Font { id: u32 }
struct TextEdit { id: u32 }

struct Rect { x: f32, y: f32, w: f32, h: f32 }
struct Color { r: u8, g: u8, b: u8, a: u8 }
struct Colorf { r: f32, g: f32, b: f32, a: f32 }
struct Scroll { x: u32, y: u32 }

// A texture, or part of one, drawn by a widget: `texture` is what the
// renderer draws with, a gfx texture handle for render() below; w and h its
// size and region the part shown (x, y, w, h), for a subimage.
struct Image { texture: u32 = 0, w: u16 = 0, h: u16 = 0, region: u16[4] = [0; 4] }
// An image drawn in nine parts, its borders l, t, r, b wide kept as they
// are and the rest stretched.
struct NineSlice { img: Image, l: u16, t: u16, r: u16, b: u16 }
// A software cursor: an image, its size, and its hot spot.
struct Cursor { img: Image, size: float2, offset: float2 }

// How a font is baked (Nuklear's nk_font_config).
struct FontConfig {
    oversample_h: u8 = 1,           // rasterized wider, for sub-pixel positions
    oversample_v: u8 = 1,
    pixel_snap: bool = false,       // glyphs on whole pixels
    merge_mode: bool = false,       // the glyphs go into the atlas's first font
    spacing: float2 = float2 { 0.0, 0.0 },
    ranges: i32 = 0,                // a RANGE_* set, unless ranges are given
    fallback_glyph: u32 = 63,       // '?', drawn for a character the font lacks
}

// A font's metrics and glyphs at its own size, whatever scale it was baked
// at; a glyph's u and v are in the atlas's image.
struct FontInfo {
    height: f32,
    ascent: f32,
    descent: f32,
    glyph_count: i32,
    fallback_codepoint: u32,        // what a character the font lacks is drawn as
}

struct FontGlyph {
    codepoint: u32,
    xadvance: f32,
    x0: f32, y0: f32, x1: f32, y1: f32, w: f32, h: f32,
    u0: f32, v0: f32, u1: f32, v1: f32,
}

// The mouse as Nuklear has it this frame.
struct Mouse {
    pos: float2,
    prev: float2,
    delta: float2,
    scroll_delta: float2,
    grab: bool,
    grabbed: bool,
    ungrab: bool,
}

// One of the commands() the widgets drew, flattened: the fields a kind has
// are the ones that mean something for it.
struct Command {
    kind: i32,                      // COMMAND_*
    // scissor, rects, circles, image, text: the box; arc: its center and radius
    x: i32, y: i32, w: i32, h: i32,
    line_thickness: i32,
    rounding: i32,
    points: int2[4],                // line, curve, triangle: its points
    a_min: f32, a_max: f32,         // arc: its angles
    color: Color,                   // stroke, fill, text; image: its tint
    background: Color,              // text: what it is drawn on
    left: Color, top: Color, right: Color, bottom: Color,   // rect_multi_color
    first: i32, count: i32,         // polygons: command_points(); text: command_text()
    image: Image,
    font: Font,                     // text
    height: f32,                    // text: the font's height
}

// How convert() turns the commands into triangles.
struct ConvertConfig {
    global_alpha: f32 = 1.0,
    line_aa: bool = true,           // anti-aliased edges
    shape_aa: bool = true,
    circle_segment_count: i32 = 22,
    arc_segment_count: i32 = 22,
    curve_segment_count: i32 = 22,
}

// What convert() produces: vertices, u32 indices, and draw calls taking
// elem_count indices each, in order, with a scissor and a texture.
struct Vertex { pos: float2, uv: float2, color: Color }
struct DrawCommand { elem_count: u32, clip_rect: Rect, texture: u32 }

// The rows of a list view in view: begin to end, count of them.
struct ListView { begin: i64, end: i64, count: i64 }

struct TextEditState {
    cursor: i64,                    // in characters, not bytes
    select_start: i64,
    select_end: i64,
    mode: i64,                      // TEXT_EDIT_MODE_*
    length: i64,                    // in characters
    single_line: bool,
    active: bool,
}

// --- style: Nuklear's nk_style, read and written whole -----------------------

// A background: a color, an image, or an image in nine parts (img with the
// borders l, t, r, b), by kind (STYLE_ITEM_*). style_item_* make them.
struct StyleItem {
    kind: i32 = 0,
    color: Color = Color { 0, 0, 0, 0 },
    img: Image = Image {},
    l: u16 = 0, t: u16 = 0, r: u16 = 0, b: u16 = 0,
}

struct StyleText { color: Color, padding: float2, color_factor: f32, disabled_factor: f32 }

struct StyleLink {
    text_normal: Color, text_hover: Color, text_active: Color,
    underline: i32,
    underline_thickness: f32,
    padding: float2, touch_padding: float2,
    color_factor: f32, disabled_factor: f32,
}

struct StyleButton {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    color_factor_background: f32,
    text_background: Color, text_normal: Color, text_hover: Color, text_active: Color,
    text_alignment: u32,
    color_factor_text: f32,
    border: f32, rounding: f32,
    padding: float2, image_padding: float2, touch_padding: float2,
    disabled_factor: f32,
}

struct StyleToggle {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    cursor_normal: StyleItem, cursor_hover: StyleItem,
    text_normal: Color, text_hover: Color, text_active: Color, text_background: Color,
    text_alignment: u32,
    padding: float2, touch_padding: float2,
    spacing: f32, border: f32, color_factor: f32, disabled_factor: f32,
}

struct StyleSelectable {
    normal: StyleItem, hover: StyleItem, pressed: StyleItem,
    normal_active: StyleItem, hover_active: StyleItem, pressed_active: StyleItem,
    text_normal: Color, text_hover: Color, text_pressed: Color,
    text_normal_active: Color, text_hover_active: Color, text_pressed_active: Color,
    text_background: Color,
    text_alignment: u32,
    rounding: f32,
    padding: float2, touch_padding: float2, image_padding: float2,
    color_factor: f32, disabled_factor: f32,
}

struct StyleSlider {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    bar_normal: Color, bar_hover: Color, bar_active: Color, bar_filled: Color,
    cursor_normal: StyleItem, cursor_hover: StyleItem, cursor_active: StyleItem,
    border: f32, rounding: f32, bar_height: f32,
    padding: float2, spacing: float2, cursor_size: float2,
    color_factor: f32, disabled_factor: f32,
    show_buttons: bool,
    inc_button: StyleButton, dec_button: StyleButton,
    inc_symbol: i32, dec_symbol: i32,
}

struct StyleKnob {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    knob_normal: Color, knob_hover: Color, knob_active: Color, knob_border_color: Color,
    cursor_normal: Color, cursor_hover: Color, cursor_active: Color,
    border: f32, knob_border: f32,
    padding: float2, spacing: float2,
    cursor_width: f32, color_factor: f32, disabled_factor: f32,
}

struct StyleProgress {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    cursor_normal: StyleItem, cursor_hover: StyleItem, cursor_active: StyleItem,
    cursor_border_color: Color,
    rounding: f32, border: f32, cursor_border: f32, cursor_rounding: f32,
    padding: float2,
    color_factor: f32, disabled_factor: f32,
}

struct StyleScrollbar {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    cursor_normal: StyleItem, cursor_hover: StyleItem, cursor_active: StyleItem,
    cursor_border_color: Color,
    border: f32, rounding: f32, border_cursor: f32, rounding_cursor: f32,
    padding: float2,
    color_factor: f32, disabled_factor: f32,
    show_buttons: bool,
    inc_button: StyleButton, dec_button: StyleButton,
    inc_symbol: i32, dec_symbol: i32,
}

struct StyleEdit {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    scrollbar: StyleScrollbar,
    cursor_normal: Color, cursor_hover: Color, cursor_text_normal: Color, cursor_text_hover: Color,
    text_normal: Color, text_hover: Color, text_active: Color,
    selected_normal: Color, selected_hover: Color,
    selected_text_normal: Color, selected_text_hover: Color,
    border: f32, rounding: f32, cursor_size: f32,
    scrollbar_size: float2, padding: float2,
    row_padding: f32, color_factor: f32, disabled_factor: f32,
}

struct StyleProperty {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    label_normal: Color, label_hover: Color, label_active: Color,
    sym_left: i32, sym_right: i32,
    border: f32, rounding: f32,
    padding: float2,
    color_factor: f32, disabled_factor: f32,
    edit: StyleEdit,
    inc_button: StyleButton, dec_button: StyleButton,
}

struct StyleChart {
    background: StyleItem,
    border_color: Color, selected_color: Color, color: Color,
    border: f32, rounding: f32,
    padding: float2,
    color_factor: f32, disabled_factor: f32,
    show_markers: bool,
}

struct StyleCombo {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    border_color: Color,
    label_normal: Color, label_hover: Color, label_active: Color,
    symbol_normal: Color, symbol_hover: Color, symbol_active: Color,
    button: StyleButton,
    sym_normal: i32, sym_hover: i32, sym_active: i32,
    border: f32, rounding: f32,
    content_padding: float2, button_padding: float2, spacing: float2,
    color_factor: f32, disabled_factor: f32,
}

struct StyleTab {
    background: StyleItem,
    border_color: Color, text: Color,
    tab_maximize_button: StyleButton, tab_minimize_button: StyleButton,
    node_maximize_button: StyleButton, node_minimize_button: StyleButton,
    sym_minimize: i32, sym_maximize: i32,
    border: f32, rounding: f32, indent: f32,
    padding: float2, spacing: float2,
    color_factor: f32, disabled_factor: f32,
}

struct StyleWindowHeader {
    normal: StyleItem, hover: StyleItem, active: StyleItem,
    close_button: StyleButton, minimize_button: StyleButton,
    close_symbol: i32, minimize_symbol: i32, maximize_symbol: i32,
    label_normal: Color, label_hover: Color, label_active: Color,
    align: i32,                     // HEADER_LEFT or HEADER_RIGHT: where its buttons go
    padding: float2, label_padding: float2, spacing: float2,
}

struct StyleWindow {
    header: StyleWindowHeader,
    fixed_background: StyleItem,
    background: Color,
    border_color: Color, popup_border_color: Color, combo_border_color: Color,
    contextual_border_color: Color, menu_border_color: Color, group_border_color: Color,
    tooltip_border_color: Color,
    scaler: StyleItem,
    border: f32, combo_border: f32, contextual_border: f32, menu_border: f32, group_border: f32,
    tooltip_border: f32, popup_border: f32, min_row_height_padding: f32,
    rounding: f32,
    spacing: float2, scrollbar_size: float2, min_size: float2,
    padding: float2, group_padding: float2, popup_padding: float2, combo_padding: float2,
    contextual_padding: float2, menu_padding: float2, tooltip_padding: float2,
    tooltip_origin: i32,            // TOP_LEFT ... BOTTOM_RIGHT
    tooltip_offset: float2,
    tooltip_delay: f32,             // seconds, for do_tooltip_delay
}

// A context's whole style, less its font and cursors, which have functions
// of their own. Color factors are from 0 to 1.
struct Style {
    text: StyleText,
    link: StyleLink,
    button: StyleButton, contextual_button: StyleButton, menu_button: StyleButton,
    option: StyleToggle, checkbox: StyleToggle,
    selectable: StyleSelectable,
    slider: StyleSlider,
    knob: StyleKnob,
    progress: StyleProgress,
    property: StyleProperty,
    edit: StyleEdit,
    chart: StyleChart,
    scrollh: StyleScrollbar, scrollv: StyleScrollbar,
    tab: StyleTab,
    combo: StyleCombo,
    window: StyleWindow,
}

// --- the library -------------------------------------------------------------

// Whether the ui layer is linked in. A compiler without it refuses to run a
// ui program in-process, so this is true wherever it runs.
extern "gs_ui_available" fn available() -> bool;
extern "gs_ui_error" fn error_into(out: u8[:]) -> i64;
extern "gs_ui_misuse_count" fn misuse_count() -> i64;
// Why the last call that failed did, as text.
fn error() -> u8[>..] {
    var text: u8[>..] = [];
    text.resize(1024, 0);
    let n = error_into(text);
    text.resize(min(n, 1024));
    return text;
}
// Aborts, saying why, if the program has used ui wrongly.
fn check() {
    if misuse_count() > 0 { abort(str("ui: ", error())); }
}

// --- font atlases and fonts --------------------------------------------------

// Fonts are baked into an atlas, one image holding the glyphs of all its
// fonts: made, fonts added, baked, and then usable by any context. render()
// uploads a baked atlas's image to the GPU when it first draws with it.
extern "gs_ui_create_font_atlas" fn create_font_atlas() -> FontAtlas;
// Destroys the atlas and its fonts; not while a context uses them.
extern "gs_ui_destroy_font_atlas" fn destroy(a: FontAtlas);
extern "gs_ui_font_atlas_is_valid" fn is_valid(a: FontAtlas) -> bool;
// Nuklear's own FontConfig defaults, which the field defaults repeat.
extern "gs_ui_default_font_config" fn default_font_config() -> FontConfig;
// Nuklear's built-in font, ProggyClean, `height` pixels high. Given
// `ranges`, pairs of first and last character, it has those characters in
// place of config.ranges.
extern "gs_ui_add_default_font" fn add_default_font(a: FontAtlas, height: f32,
                                                    config: FontConfig,
                                                    ranges: const u32[:]) -> Font;
fn add_default_font(a: FontAtlas, height: f32, config: FontConfig) -> Font {
    let none: u32[>..] = [];
    add_default_font(a, height, config, none)
}
fn add_default_font(a: FontAtlas, height: f32) -> Font {
    add_default_font(a, height, FontConfig {})
}
// A TrueType or OpenType font from its bytes (read_file gives them).
extern "gs_ui_add_font_from_memory" fn add_font_from_memory(a: FontAtlas, ttf: const u8[:],
                                                            height: f32,
                                                            config: FontConfig,
                                                            ranges: const u32[:]) -> Font;
fn add_font_from_memory(a: FontAtlas, ttf: const u8[:], height: f32, config: FontConfig) -> Font {
    let none: u32[>..] = [];
    add_font_from_memory(a, ttf, height, config, none)
}
fn add_font_from_memory(a: FontAtlas, ttf: const u8[:], height: f32) -> Font {
    add_font_from_memory(a, ttf, height, FontConfig {})
}
// A font file, or a zero handle with error() set.
extern "gs_ui_add_font_from_file" fn add_font_from_file(a: FontAtlas, path: const u8[:],
                                                        height: f32, config: FontConfig,
                                                        ranges: const u32[:]) -> Font;
fn add_font_from_file(a: FontAtlas, path: const u8[:], height: f32, config: FontConfig) -> Font {
    let none: u32[>..] = [];
    add_font_from_file(a, path, height, config, none)
}
fn add_font_from_file(a: FontAtlas, path: const u8[:], height: f32) -> Font {
    add_font_from_file(a, path, height, FontConfig {})
}
// Rasterizes the fonts added into the atlas's image; no fonts are added
// after. At a `scale`, the glyphs are that many times the fonts' size
// while the fonts measure at their own: sharp text for a ui drawn with
// set_scale at the same scale.
extern "gs_ui_bake_font_atlas" fn bake(a: FontAtlas, scale: f32) -> bool;
fn bake(a: FontAtlas) -> bool { bake(a, 1.0) }
extern "gs_ui_font_atlas_is_baked" fn is_baked(a: FontAtlas) -> bool;
// The baked image: RGBA, width x height.
extern "gs_ui_font_atlas_size" fn atlas_size_into(a: FontAtlas, out: int2&);
fn atlas_size(a: FontAtlas) -> int2 {
    var size = int2_0;
    atlas_size_into(a, size);
    size
}
extern "gs_ui_font_atlas_pixels" fn atlas_pixels_into(a: FontAtlas, out: u8[:]) -> i64;
fn atlas_pixels(a: FontAtlas) -> u8[>..] {
    var px: u8[>..] = [];
    let n = atlas_pixels_into(a, px);
    px.resize(n, 0);
    atlas_pixels_into(a, px);
    return px;
}
// The texture a renderer uploaded the image to, which the atlas's text is
// drawn with; 0 for none yet.
extern "gs_ui_set_font_atlas_texture" fn set_texture(a: FontAtlas, texture: u32);
extern "gs_ui_font_atlas_texture" fn texture(a: FontAtlas) -> u32;
// A baked atlas without a texture, or a zero handle: what a renderer has
// yet to upload.
extern "gs_ui_font_atlas_without_texture" fn atlas_without_texture() -> FontAtlas;
// Forgets every atlas's texture, for a renderer whose textures are gone.
extern "gs_ui_forget_font_atlas_textures" fn forget_textures();
extern "gs_ui_released_textures" fn released_textures_into(out: u32[:]) -> i64;
// The textures of the atlases destroyed since the last call, for the
// renderer that made them to release.
fn released_textures() -> u32[>..] {
    var out: u32[>..] = [];
    let n = released_textures_into(out);
    out.resize(n, 0);
    released_textures_into(out);
    return out;
}
extern "gs_ui_font_atlas_fonts" fn fonts_into(a: FontAtlas, out: Font[:]) -> i64;
// The atlas's fonts, in the order added.
fn fonts(a: FontAtlas) -> Font[>..] {
    var out: Font[>..] = [];
    let n = fonts_into(a, out);
    out.resize(n, Font { 0 });
    fonts_into(a, out);
    return out;
}
extern "gs_ui_font_atlas_of" fn atlas(f: Font) -> FontAtlas;
extern "gs_ui_font_is_valid" fn is_valid(f: Font) -> bool;
// Metrics at the size the font was baked, once it is.
extern "gs_ui_font_info_of" fn info(f: Font) -> FontInfo;
extern "gs_ui_font_find_glyph" fn find_glyph_into(f: Font, codepoint: u32,
                                                  out: FontGlyph&) -> bool;
// The glyph drawn for a character, and whether it is the character's own
// rather than the fallback.
fn find_glyph(f: Font, codepoint: u32) -> FontGlyph, bool {
    var g = default<FontGlyph>();
    let own = find_glyph_into(f, codepoint, g);
    return g, own;
}
// How wide `text` is drawn in this font, in pixels.
extern "gs_ui_font_text_width" fn text_width(f: Font, text: const u8[:]) -> f32;
extern "gs_ui_font_glyph_ranges" fn glyph_ranges_into(which: i64, out: u32[:]) -> i64;
// The characters of a RANGE_* set, as pairs of first and last.
fn glyph_ranges(which: i64) -> u32[>..] {
    var out: u32[>..] = [];
    let n = glyph_ranges_into(which, out);
    out.resize(n, 0);
    glyph_ranges_into(which, out);
    return out;
}

// --- contexts ----------------------------------------------------------------

// A context drawing with `font`, from a baked atlas; its windows and their
// state live in it.
extern "gs_ui_create_context" fn create_context(font: Font) -> Context;
// A context drawing with Nuklear's built-in font at `font_height` (13 is
// its native size), in an atlas made for it and destroyed with it; given a
// scale, drawn at it (set_scale).
extern "gs_ui_create_default_context" fn create(font_height: f32) -> Context;
fn create(font_height: f32, scale: f32) -> Context {
    let c = create(font_height);
    set_scale(c, scale);
    c
}
// The atlas of the font the context was made with, whose white texel
// draws its shapes.
extern "gs_ui_context_atlas" fn context_atlas(c: Context) -> FontAtlas;
extern "gs_ui_destroy_context" fn destroy_raw(c: Context);
fn destroy(c: Context) {
    check();
    destroy_raw(c);
}
extern "gs_ui_context_is_valid" fn is_valid(c: Context) -> bool;
// Ends a frame: drops what it drew, and the windows not begun in it.
// input_begin does it too, for a frame that drew anything.
extern "gs_ui_clear" fn clear(c: Context);
// The seconds the frame took, which hides scroll bars and times tooltips;
// input_from_gfx sets it.
extern "gs_ui_set_delta_time" fn set_delta_time(c: Context, seconds: f32);
extern "gs_ui_delta_time" fn delta_time(c: Context) -> f32;
// The font the context draws with now.
extern "gs_ui_context_font" fn font(c: Context) -> Font;
// Text edits paste this, on KEY_PASTE with EDIT_CLIPBOARD.
extern "gs_ui_set_clipboard" fn set_clipboard(c: Context, text: const u8[:]);
extern "gs_ui_copied" fn copied_into(c: Context, out: u8[:]) -> i64;
// What a text edit copied or cut since the last call, and whether there
// was any: for the program to put on the system clipboard.
fn copied(c: Context) -> u8[>..], bool {
    var text: u8[>..] = [];
    let n = copied_into(c, text);
    if n < 0 { return text, false; }
    if n > 0 {
        text.resize(n, 0);
        copied_into(c, text);
    }
    return text, true;
}
// The texture shapes are drawn with: the one holding its atlas's image.
extern "gs_ui_null_texture" fn null_texture(c: Context) -> u32;
// The whole ui `scale` times bigger: laid out as before, in units of the
// ui's own (window rectangles, rows, the font's size), and drawn with that
// many pixels to each, which convert() and render() produce and input
// comes in. A context made by create() has its font baked again at the
// scale, which gives it a new font handle; another's text is as sharp as
// its atlas was baked (bake(atlas, scale)). Not during a frame; 1 at
// first.
extern "gs_ui_set_scale" fn set_scale(c: Context, scale: f32) -> bool;
extern "gs_ui_scale" fn scale(c: Context) -> f32;

// --- input -------------------------------------------------------------------

// A frame's input goes between input_begin and input_end, before any
// window; input_from_gfx below does it all from gfx's. Positions are
// pixels, which the ui divides by its scale.
extern "gs_ui_input_begin" fn input_begin_raw(c: Context);
fn input_begin(c: Context) {
    input_begin_raw(c);
    check();
}
extern "gs_ui_input_motion" fn input_motion(c: Context, x: i64, y: i64);
extern "gs_ui_input_key" fn input_key(c: Context, key: i64, down: bool);
extern "gs_ui_input_button" fn input_button(c: Context, button: i64, x: i64, y: i64, down: bool);
extern "gs_ui_input_scroll" fn input_scroll(c: Context, amount: float2);
extern "gs_ui_input_char" fn input_char(c: Context, ch: u8);
extern "gs_ui_input_glyph" fn input_glyph(c: Context, utf8: const u8[:]);
extern "gs_ui_input_unicode" fn input_unicode(c: Context, rune: u32);
extern "gs_ui_input_end" fn input_end(c: Context);
// Typed text, a character at a time.
fn input_text(c: Context, text: const u8[:]) {
    each_utf8(text) { input_unicode(c, it as u32); };
}

// The input this frame, as widgets see it, in the ui's own units. A click
// is a button going down or up, and a release completes one: the has_
// queries look at where the button's last click was, whenever that was,
// and the is_ ones at a click this frame; has_mouse_click is a release
// this frame.
extern "gs_ui_input_mouse" fn input_mouse(c: Context) -> Mouse;
extern "gs_ui_input_has_mouse_click" fn input_has_mouse_click(c: Context, button: i64) -> bool;
extern "gs_ui_input_has_mouse_click_in_rect" fn input_has_mouse_click_in_rect(c: Context,
                                                                              button: i64,
                                                                              r: Rect) -> bool;
extern "gs_ui_input_has_mouse_click_in_button_rect" fn input_has_mouse_click_in_button_rect(
    c: Context, button: i64, r: Rect) -> bool;
extern "gs_ui_input_has_mouse_click_down_in_rect" fn input_has_mouse_click_down_in_rect(
    c: Context, button: i64, r: Rect, down: bool) -> bool;
extern "gs_ui_input_is_mouse_click_in_rect" fn input_is_mouse_click_in_rect(c: Context,
                                                                            button: i64,
                                                                            r: Rect) -> bool;
extern "gs_ui_input_is_mouse_click_down_in_rect" fn input_is_mouse_click_down_in_rect(
    c: Context, button: i64, r: Rect, down: bool) -> bool;
extern "gs_ui_input_any_mouse_click_in_rect" fn input_any_mouse_click_in_rect(c: Context,
                                                                              r: Rect) -> bool;
extern "gs_ui_input_is_mouse_prev_hovering_rect" fn input_is_mouse_prev_hovering_rect(
    c: Context, r: Rect) -> bool;
extern "gs_ui_input_is_mouse_hovering_rect" fn input_is_mouse_hovering_rect(c: Context,
                                                                            r: Rect) -> bool;
extern "gs_ui_input_is_mouse_hovering_still_rect" fn input_is_mouse_hovering_still_rect(
    c: Context, r: Rect) -> bool;
// Whether the mouse has hovered `r` for `delay` seconds, counting in
// `timer`, which the program keeps from frame to frame; the _still ones
// only while it does not move, the _clicked one not after `clicked`.
extern "gs_ui_input_is_mouse_hovering_delay_rect" fn input_is_mouse_hovering_delay_rect(
    c: Context, r: Rect, timer: f32&, delay: f32) -> bool;
extern "gs_ui_input_is_mouse_hovering_still_delay_rect" fn input_is_mouse_hovering_still_delay_rect(
    c: Context, r: Rect, timer: f32&, delay: f32) -> bool;
extern "gs_ui_input_is_mouse_hovering_still_delay_clicked_rect" fn input_is_mouse_hovering_still_delay_clicked_rect(
    c: Context, r: Rect, timer: f32&, delay: f32, clicked: bool&) -> bool;
extern "gs_ui_input_is_mouse_moved" fn input_is_mouse_moved(c: Context) -> bool;
extern "gs_ui_input_mouse_clicked" fn input_mouse_clicked(c: Context, button: i64,
                                                          r: Rect) -> bool;
extern "gs_ui_input_is_mouse_down" fn input_is_mouse_down(c: Context, button: i64) -> bool;
extern "gs_ui_input_is_mouse_pressed" fn input_is_mouse_pressed(c: Context, button: i64) -> bool;
extern "gs_ui_input_is_mouse_released" fn input_is_mouse_released(c: Context, button: i64) -> bool;
extern "gs_ui_input_is_key_pressed" fn input_is_key_pressed(c: Context, key: i64) -> bool;
extern "gs_ui_input_is_key_released" fn input_is_key_released(c: Context, key: i64) -> bool;
extern "gs_ui_input_is_key_down" fn input_is_key_down(c: Context, key: i64) -> bool;

// --- what was drawn ----------------------------------------------------------

// The frame's command list, once its windows have ended: what a renderer
// of its own draws, and what tests look at.
extern "gs_ui_commands" fn commands_into(c: Context, out: Command[:]) -> i64;
extern "gs_ui_command_points" fn command_points_into(c: Context, out: int2[:]) -> i64;
extern "gs_ui_command_text" fn command_text_into(c: Context, out: u8[:]) -> i64;
fn commands(c: Context) -> Command[>..] {
    var out: Command[>..] = [];
    let n = commands_into(c, out);
    out.resize(n, default<Command>());
    commands_into(c, out);
    return out;
}
// The points of the polygons and polylines, which a command's first and
// count index.
fn command_points(c: Context) -> int2[>..] {
    var out: int2[>..] = [];
    let n = command_points_into(c, out);
    out.resize(n, int2_0);
    command_points_into(c, out);
    return out;
}
// The texts' bytes, which a text command's first and count index.
fn command_text(c: Context) -> u8[>..] {
    var out: u8[>..] = [];
    let n = command_text_into(c, out);
    out.resize(n, 0);
    command_text_into(c, out);
    return out;
}

// The frame as triangles, for a renderer: convert, then vertices, indices
// and draw_commands, which stay until the next convert or clear. Returns a
// CONVERT_* result.
extern "gs_ui_convert" fn convert_raw(c: Context, config: const ConvertConfig&) -> i64;
fn convert(c: Context, config: ConvertConfig) -> i64 {
    let r = convert_raw(c, config);
    check();
    r
}
extern "gs_ui_vertices" fn vertices_into(c: Context, out: Vertex[:]) -> i64;
extern "gs_ui_indices" fn indices_into(c: Context, out: u32[:]) -> i64;
extern "gs_ui_draw_commands" fn draw_commands_into(c: Context, out: DrawCommand[:]) -> i64;
fn vertices(c: Context) -> Vertex[>..] {
    var out: Vertex[>..] = [];
    let n = vertices_into(c, out);
    out.resize(n, default<Vertex>());
    vertices_into(c, out);
    return out;
}
fn indices(c: Context) -> u32[>..] {
    var out: u32[>..] = [];
    let n = indices_into(c, out);
    out.resize(n, 0);
    indices_into(c, out);
    return out;
}
fn draw_commands(c: Context) -> DrawCommand[>..] {
    var out: DrawCommand[>..] = [];
    let n = draw_commands_into(c, out);
    out.resize(n, default<DrawCommand>());
    draw_commands_into(c, out);
    return out;
}

// --- windows -----------------------------------------------------------------

// A window, named by its title: true when it is open to fill with widgets.
// end() follows either way.
extern "gs_ui_begin" fn begin(c: Context, title: const u8[:], bounds: Rect, flags: i64) -> bool;
// A window named `name`, showing `title`, so the title can change.
extern "gs_ui_begin_titled" fn begin_titled(c: Context, name: const u8[:], title: const u8[:],
                                            bounds: Rect, flags: i64) -> bool;
extern "gs_ui_end" fn end(c: Context);
// Whether a window of that name exists.
extern "gs_ui_window_find" fn window_find(c: Context, name: const u8[:]) -> bool;
// The window being built.
extern "gs_ui_window_get_bounds" fn window_get_bounds(c: Context) -> Rect;
extern "gs_ui_window_get_position" fn window_get_position(c: Context) -> float2;
extern "gs_ui_window_get_size" fn window_get_size(c: Context) -> float2;
extern "gs_ui_window_get_width" fn window_get_width(c: Context) -> f32;
extern "gs_ui_window_get_height" fn window_get_height(c: Context) -> f32;
// The area widgets go in: the window less its title, border and scroll bars.
extern "gs_ui_window_get_content_region" fn window_get_content_region(c: Context) -> Rect;
extern "gs_ui_window_get_content_region_min" fn window_get_content_region_min(c: Context) -> float2;
extern "gs_ui_window_get_content_region_max" fn window_get_content_region_max(c: Context) -> float2;
extern "gs_ui_window_get_content_region_size" fn window_get_content_region_size(
    c: Context) -> float2;
extern "gs_ui_window_get_scroll" fn window_get_scroll(c: Context) -> Scroll;
extern "gs_ui_window_has_focus" fn window_has_focus(c: Context) -> bool;
extern "gs_ui_window_is_hovered" fn window_is_hovered(c: Context) -> bool;
// Windows by name.
extern "gs_ui_window_is_collapsed" fn window_is_collapsed(c: Context, name: const u8[:]) -> bool;
extern "gs_ui_window_is_closed" fn window_is_closed(c: Context, name: const u8[:]) -> bool;
extern "gs_ui_window_is_hidden" fn window_is_hidden(c: Context, name: const u8[:]) -> bool;
extern "gs_ui_window_is_active" fn window_is_active(c: Context, name: const u8[:]) -> bool;
// Whether the mouse is over any window: whether input is the ui's, not the
// program's.
extern "gs_ui_window_is_any_hovered" fn window_is_any_hovered(c: Context) -> bool;
// That, or a widget is being used.
extern "gs_ui_item_is_any_active" fn item_is_any_active(c: Context) -> bool;
extern "gs_ui_window_set_bounds" fn window_set_bounds(c: Context, name: const u8[:], bounds: Rect);
extern "gs_ui_window_set_position" fn window_set_position(c: Context, name: const u8[:],
                                                          pos: float2);
extern "gs_ui_window_set_size" fn window_set_size(c: Context, name: const u8[:], size: float2);
extern "gs_ui_window_set_focus" fn window_set_focus(c: Context, name: const u8[:]);
extern "gs_ui_window_set_scroll" fn window_set_scroll(c: Context, x: i64, y: i64);
extern "gs_ui_window_close" fn window_close(c: Context, name: const u8[:]);
// MINIMIZED or MAXIMIZED.
extern "gs_ui_window_collapse" fn window_collapse(c: Context, name: const u8[:], state: i64);
extern "gs_ui_window_collapse_if" fn window_collapse_if(c: Context, name: const u8[:], state: i64,
                                                        cond: bool);
// HIDDEN or SHOWN.
extern "gs_ui_window_show" fn window_show(c: Context, name: const u8[:], state: i64);
extern "gs_ui_window_show_if" fn window_show_if(c: Context, name: const u8[:], state: i64,
                                                cond: bool);
// A line across the window.
extern "gs_ui_rule_horizontal" fn rule_horizontal(c: Context, color: Color, rounding: bool);

// --- layout ------------------------------------------------------------------

// Widgets fill rows left to right: a row's layout says how many columns and
// how wide.
extern "gs_ui_layout_set_min_row_height" fn layout_set_min_row_height(c: Context, height: f32);
extern "gs_ui_layout_reset_min_row_height" fn layout_reset_min_row_height(c: Context);
// Where the next widget goes.
extern "gs_ui_layout_widget_bounds" fn layout_widget_bounds(c: Context) -> Rect;
extern "gs_ui_layout_ratio_from_pixel" fn layout_ratio_from_pixel(c: Context,
                                                                  pixel_width: f32) -> f32;
// `cols` columns sharing the width, or `item_width` pixels each.
extern "gs_ui_layout_row_dynamic" fn layout_row_dynamic(c: Context, height: f32, cols: i64);
extern "gs_ui_layout_row_static" fn layout_row_static(c: Context, height: f32, item_width: i64,
                                                      cols: i64);
// A row whose widths are pushed one at a time, as ratios (DYNAMIC) or
// pixels (STATIC), each before its widget.
extern "gs_ui_layout_row_begin" fn layout_row_begin(c: Context, format: i64, row_height: f32,
                                                    cols: i64);
extern "gs_ui_layout_row_push" fn layout_row_push(c: Context, value: f32);
extern "gs_ui_layout_row_end" fn layout_row_end(c: Context);
// A row with a column per ratio or width.
extern "gs_ui_layout_row" fn layout_row(c: Context, format: i64, height: f32, ratios: const f32[:]);
// A row of columns of fixed, variable or remaining width, up to 16.
extern "gs_ui_layout_row_template_begin" fn layout_row_template_begin(c: Context, row_height: f32);
extern "gs_ui_layout_row_template_push_dynamic" fn layout_row_template_push_dynamic(c: Context);
extern "gs_ui_layout_row_template_push_variable" fn layout_row_template_push_variable(
    c: Context, min_width: f32);
extern "gs_ui_layout_row_template_push_static" fn layout_row_template_push_static(c: Context,
                                                                                  width: f32);
extern "gs_ui_layout_row_template_end" fn layout_row_template_end(c: Context);
// A space `height` high where widgets go where layout_space_push puts
// them, in pixels (STATIC) or parts of the space (DYNAMIC).
extern "gs_ui_layout_space_begin" fn layout_space_begin(c: Context, format: i64, height: f32,
                                                        widget_count: i64);
extern "gs_ui_layout_space_push" fn layout_space_push(c: Context, bounds: Rect);
extern "gs_ui_layout_space_end" fn layout_space_end(c: Context);
extern "gs_ui_layout_space_bounds" fn layout_space_bounds(c: Context) -> Rect;
extern "gs_ui_layout_space_to_screen" fn layout_space_to_screen(c: Context, v: float2) -> float2;
extern "gs_ui_layout_space_to_local" fn layout_space_to_local(c: Context, v: float2) -> float2;
extern "gs_ui_layout_space_rect_to_screen" fn layout_space_rect_to_screen(c: Context,
                                                                          r: Rect) -> Rect;
extern "gs_ui_layout_space_rect_to_local" fn layout_space_rect_to_local(c: Context,
                                                                        r: Rect) -> Rect;
// An empty place in the row.
extern "gs_ui_spacer" fn spacer(c: Context);

// --- groups ------------------------------------------------------------------

// A scrolling area inside a window, as one widget of the row: group_end
// follows when it returns true.
extern "gs_ui_group_begin" fn group_begin(c: Context, title: const u8[:], flags: i64) -> bool;
extern "gs_ui_group_begin_titled" fn group_begin_titled(c: Context, id: const u8[:],
                                                        title: const u8[:], flags: i64) -> bool;
extern "gs_ui_group_end" fn group_end(c: Context);
// A group whose scroll offset the program keeps: given to begin, and what
// the user scrolled it to back from end.
extern "gs_ui_group_scrolled_begin" fn group_scrolled_begin(c: Context, offset: Scroll,
                                                            title: const u8[:], flags: i64) -> bool;
extern "gs_ui_group_scrolled_end" fn group_scrolled_end(c: Context) -> Scroll;
extern "gs_ui_group_get_scroll" fn group_get_scroll(c: Context, id: const u8[:]) -> Scroll;
extern "gs_ui_group_set_scroll" fn group_set_scroll(c: Context, id: const u8[:], x: i64, y: i64);

// --- trees -------------------------------------------------------------------

// A collapsible node or tab (TREE_NODE, TREE_TAB): true while open, when
// tree_pop follows. Its state is kept by a hash of `hash` (or, empty, the
// title) and `seed`: nodes with the same title need a different one.
extern "gs_ui_tree_push_hashed" fn tree_push_hashed(c: Context, tree_type: i64,
                                                    title: const u8[:], initial_state: i64,
                                                    hash: const u8[:], seed: i64) -> bool;
fn tree_push(c: Context, tree_type: i64, title: const u8[:], initial_state: i64) -> bool {
    tree_push_hashed(c, tree_type, title, initial_state, "", 0)
}
fn tree_push_id(c: Context, tree_type: i64, title: const u8[:], initial_state: i64,
                id: i64) -> bool {
    tree_push_hashed(c, tree_type, title, initial_state, "", id)
}
extern "gs_ui_tree_image_push_hashed" fn tree_image_push_hashed(c: Context, tree_type: i64,
                                                                img: Image, title: const u8[:],
                                                                initial_state: i64,
                                                                hash: const u8[:],
                                                                seed: i64) -> bool;
fn tree_image_push(c: Context, tree_type: i64, img: Image, title: const u8[:],
                   initial_state: i64) -> bool {
    tree_image_push_hashed(c, tree_type, img, title, initial_state, "", 0)
}
extern "gs_ui_tree_pop" fn tree_pop(c: Context);
// A node whose state (MINIMIZED, MAXIMIZED) the program keeps.
extern "gs_ui_tree_state_push" fn tree_state_push(c: Context, tree_type: i64, title: const u8[:],
                                                  state: i64&) -> bool;
extern "gs_ui_tree_state_image_push" fn tree_state_image_push(c: Context, tree_type: i64,
                                                              img: Image, title: const u8[:],
                                                              state: i64&) -> bool;
extern "gs_ui_tree_state_pop" fn tree_state_pop(c: Context);
// A node that is also selectable.
extern "gs_ui_tree_element_push_hashed" fn tree_element_push_hashed(c: Context, tree_type: i64,
                                                                    title: const u8[:],
                                                                    initial_state: i64,
                                                                    selected: bool&,
                                                                    hash: const u8[:],
                                                                    seed: i64) -> bool;
fn tree_element_push(c: Context, tree_type: i64, title: const u8[:], initial_state: i64,
                     selected: bool&) -> bool {
    tree_element_push_hashed(c, tree_type, title, initial_state, selected, "", 0)
}
extern "gs_ui_tree_element_image_push_hashed" fn tree_element_image_push_hashed(
    c: Context, tree_type: i64, img: Image, title: const u8[:], initial_state: i64,
    selected: bool&, hash: const u8[:], seed: i64) -> bool;
extern "gs_ui_tree_element_pop" fn tree_element_pop(c: Context);

// --- list views --------------------------------------------------------------

// A scrolling list of row_count rows, of which only the ones in view are
// built: begin to end of the view returned. list_view_end follows when true.
extern "gs_ui_list_view_begin" fn list_view_begin_into(c: Context, out: ListView&, id: const u8[:],
                                                       flags: i64, row_height: i64,
                                                       row_count: i64) -> bool;
fn list_view_begin(c: Context, id: const u8[:], flags: i64, row_height: i64,
                   row_count: i64) -> ListView, bool {
    var view = ListView { 0, 0, 0 };
    let open = list_view_begin_into(c, view, id, flags, row_height, row_count);
    return view, open;
}
extern "gs_ui_list_view_end" fn list_view_end(c: Context);

// --- widgets in general ------------------------------------------------------

// Takes the next place in the row for a widget of the program's own, drawn
// with the canvas functions below: whether it can be seen and used
// (WIDGET_*), and where.
extern "gs_ui_widget" fn widget_into(c: Context, bounds: Rect&) -> i64;
fn widget(c: Context) -> i64, Rect {
    var r = Rect { 0.0, 0.0, 0.0, 0.0 };
    let state = widget_into(c, r);
    return state, r;
}
// The next widget's place, without taking it.
extern "gs_ui_widget_bounds" fn widget_bounds(c: Context) -> Rect;
extern "gs_ui_widget_position" fn widget_position(c: Context) -> float2;
extern "gs_ui_widget_size" fn widget_size(c: Context) -> float2;
extern "gs_ui_widget_width" fn widget_width(c: Context) -> f32;
extern "gs_ui_widget_height" fn widget_height(c: Context) -> f32;
extern "gs_ui_widget_is_hovered" fn widget_is_hovered(c: Context) -> bool;
extern "gs_ui_widget_is_mouse_clicked" fn widget_is_mouse_clicked(c: Context,
                                                                  button: i64) -> bool;
extern "gs_ui_widget_has_mouse_click_down" fn widget_has_mouse_click_down(c: Context, button: i64,
                                                                          down: bool) -> bool;
// Skips `cols` places.
extern "gs_ui_spacing" fn spacing(c: Context, cols: i64);
// Widgets between these are shown dimmed and do nothing.
extern "gs_ui_widget_disable_begin" fn widget_disable_begin(c: Context);
extern "gs_ui_widget_disable_end" fn widget_disable_end(c: Context);

// --- text and images ---------------------------------------------------------

extern "gs_ui_label" fn label(c: Context, text: const u8[:], align: i64);
extern "gs_ui_label_colored" fn label_colored(c: Context, text: const u8[:], align: i64,
                                              color: Color);
// Wrapped onto as many lines as the widget is high.
extern "gs_ui_label_wrap" fn label_wrap(c: Context, text: const u8[:]);
extern "gs_ui_label_colored_wrap" fn label_colored_wrap(c: Context, text: const u8[:],
                                                        color: Color);
extern "gs_ui_image_widget" fn image(c: Context, img: Image);
extern "gs_ui_image_color" fn image_color(c: Context, img: Image, color: Color);
// "prefix: value", as Nuklear writes it.
extern "gs_ui_value_bool" fn value_bool(c: Context, prefix: const u8[:], value: bool);
extern "gs_ui_value_int" fn value_int(c: Context, prefix: const u8[:], value: i64);
extern "gs_ui_value_uint" fn value_uint(c: Context, prefix: const u8[:], value: u64);
extern "gs_ui_value_float" fn value_float(c: Context, prefix: const u8[:], value: f32);
extern "gs_ui_value_color_byte" fn value_color_byte(c: Context, prefix: const u8[:], color: Color);
extern "gs_ui_value_color_float" fn value_color_float(c: Context, prefix: const u8[:],
                                                      color: Color);
extern "gs_ui_value_color_hex" fn value_color_hex(c: Context, prefix: const u8[:], color: Color);
// Text that is clicked like a button.
extern "gs_ui_link_label" fn link_label(c: Context, text: const u8[:], align: i64) -> bool;
extern "gs_ui_link_label_colored" fn link_label_colored(c: Context, text: const u8[:], align: i64,
                                                        color: Color) -> bool;
// LINK_UNDERLINE_*.
extern "gs_ui_link_label_underline" fn link_label_underline(c: Context, text: const u8[:],
                                                            align: i64, underline: i64) -> bool;
fn link_label_hover_underline(c: Context, text: const u8[:], align: i64) -> bool {
    link_label_underline(c, text, align, LINK_UNDERLINE_HOVER)
}
fn link_label_no_underline(c: Context, text: const u8[:], align: i64) -> bool {
    link_label_underline(c, text, align, LINK_UNDERLINE_NONE)
}
extern "gs_ui_link_label_styled" fn link_label_styled(c: Context, style: const StyleLink&,
                                                      text: const u8[:], align: i64) -> bool;

// --- buttons -----------------------------------------------------------------

// Each is true when clicked (or, BUTTON_REPEATER, while held).
extern "gs_ui_button_label" fn button_label(c: Context, text: const u8[:]) -> bool;
extern "gs_ui_button_color" fn button_color(c: Context, color: Color) -> bool;
extern "gs_ui_button_symbol" fn button_symbol(c: Context, symbol: i64) -> bool;
extern "gs_ui_button_image" fn button_image(c: Context, img: Image) -> bool;
extern "gs_ui_button_symbol_label" fn button_symbol_label(c: Context, symbol: i64,
                                                          text: const u8[:], align: i64) -> bool;
extern "gs_ui_button_image_label" fn button_image_label(c: Context, img: Image, text: const u8[:],
                                                        align: i64) -> bool;
// In a style of their own: style(ctx).button, changed.
extern "gs_ui_button_label_styled" fn button_label_styled(c: Context, style: const StyleButton&,
                                                          text: const u8[:]) -> bool;
extern "gs_ui_button_symbol_styled" fn button_symbol_styled(c: Context,
                                                            style: const StyleButton&,
                                                            symbol: i64) -> bool;
extern "gs_ui_button_image_styled" fn button_image_styled(c: Context, style: const StyleButton&,
                                                          img: Image) -> bool;
extern "gs_ui_button_symbol_label_styled" fn button_symbol_label_styled(
    c: Context, style: const StyleButton&, symbol: i64, text: const u8[:], align: i64) -> bool;
extern "gs_ui_button_image_label_styled" fn button_image_label_styled(
    c: Context, style: const StyleButton&, img: Image, text: const u8[:], align: i64) -> bool;
// BUTTON_DEFAULT or BUTTON_REPEATER, for the buttons that follow.
extern "gs_ui_button_set_behavior" fn button_set_behavior(c: Context, behavior: i64);
extern "gs_ui_button_push_behavior" fn button_push_behavior(c: Context, behavior: i64) -> bool;
extern "gs_ui_button_pop_behavior" fn button_pop_behavior(c: Context) -> bool;

// --- check boxes, radio buttons, selectables ---------------------------------

// The check box's state now, given what it was.
extern "gs_ui_check_label" fn check_label(c: Context, text: const u8[:], active: bool) -> bool;
extern "gs_ui_check_label_align" fn check_label_align(c: Context, text: const u8[:], active: bool,
                                                      widget_align: i64, text_align: i64) -> bool;
// `flags` with `value`'s bits set or cleared.
extern "gs_ui_check_flags_label" fn check_flags_label(c: Context, text: const u8[:], flags: i64,
                                                      value: i64) -> i64;
// These change `active` (or flags), and say whether they did.
extern "gs_ui_checkbox_label" fn checkbox_label(c: Context, text: const u8[:],
                                                active: bool&) -> bool;
extern "gs_ui_checkbox_label_align" fn checkbox_label_align(c: Context, text: const u8[:],
                                                            active: bool&, widget_align: i64,
                                                            text_align: i64) -> bool;
extern "gs_ui_checkbox_flags_label" fn checkbox_flags_label(c: Context, text: const u8[:],
                                                            flags: i64&, value: i64) -> bool;
extern "gs_ui_radio_label" fn radio_label(c: Context, text: const u8[:], active: bool&) -> bool;
extern "gs_ui_radio_label_align" fn radio_label_align(c: Context, text: const u8[:],
                                                      active: bool&, widget_align: i64,
                                                      text_align: i64) -> bool;
// A radio button's state now: true when it is the one chosen.
extern "gs_ui_option_label" fn option_label(c: Context, text: const u8[:], active: bool) -> bool;
extern "gs_ui_option_label_align" fn option_label_align(c: Context, text: const u8[:],
                                                        active: bool, widget_align: i64,
                                                        text_align: i64) -> bool;
// A selectable changes `value`, and says whether it did; select returns
// its state now.
extern "gs_ui_selectable_label" fn selectable_label(c: Context, text: const u8[:], align: i64,
                                                    value: bool&) -> bool;
extern "gs_ui_selectable_image_label" fn selectable_image_label(c: Context, img: Image,
                                                                text: const u8[:], align: i64,
                                                                value: bool&) -> bool;
extern "gs_ui_selectable_symbol_label" fn selectable_symbol_label(c: Context, symbol: i64,
                                                                  text: const u8[:], align: i64,
                                                                  value: bool&) -> bool;
extern "gs_ui_select_label" fn select_label(c: Context, text: const u8[:], align: i64,
                                            value: bool) -> bool;
extern "gs_ui_select_image_label" fn select_image_label(c: Context, img: Image, text: const u8[:],
                                                        align: i64, value: bool) -> bool;
extern "gs_ui_select_symbol_label" fn select_symbol_label(c: Context, symbol: i64,
                                                          text: const u8[:], align: i64,
                                                          value: bool) -> bool;

// --- sliders, knobs, progress bars, color pickers ----------------------------

// The slide_ ones return the value now; the slider_ ones change it and
// say whether they did.
extern "gs_ui_slide_float" fn slide_float(c: Context, min: f32, value: f32, max: f32,
                                          step: f32) -> f32;
extern "gs_ui_slide_int" fn slide_int(c: Context, min: i64, value: i64, max: i64, step: i64) -> i64;
extern "gs_ui_slider_float" fn slider_float(c: Context, min: f32, value: f32&, max: f32,
                                            step: f32) -> bool;
extern "gs_ui_slider_int" fn slider_int(c: Context, min: i64, value: i64&, max: i64,
                                        step: i64) -> bool;
// A dial: turned by dragging, zero at `zero_direction`, with a dead zone
// of that many degrees there.
extern "gs_ui_knob_float" fn knob_float(c: Context, min: f32, value: f32&, max: f32, step: f32,
                                        zero_direction: i64, dead_zone_degrees: f32) -> bool;
extern "gs_ui_knob_int" fn knob_int(c: Context, min: i64, value: i64&, max: i64, step: i64,
                                    zero_direction: i64, dead_zone_degrees: f32) -> bool;
extern "gs_ui_progress" fn progress(c: Context, cur: i64&, max: i64, modifiable: bool) -> bool;
extern "gs_ui_prog" fn prog(c: Context, cur: i64, max: i64, modifiable: bool) -> i64;
// A color, RGB or RGBA: picker returns it, pick changes it.
extern "gs_ui_color_picker" fn color_picker(c: Context, color: Colorf, format: i64) -> Colorf;
extern "gs_ui_color_pick" fn color_pick(c: Context, color: Colorf&, format: i64) -> bool;

// --- properties --------------------------------------------------------------

// A number with its name, dragged, stepped with its arrows, or typed. A
// name starting with '#' is not shown, and makes a property unique by
// order rather than name.
extern "gs_ui_property_int" fn property_int(c: Context, name: const u8[:], min: i64, value: i64&,
                                            max: i64, step: i64, inc_per_pixel: f32) -> bool;
extern "gs_ui_property_float" fn property_float(c: Context, name: const u8[:], min: f32,
                                                value: f32&, max: f32, step: f32,
                                                inc_per_pixel: f32) -> bool;
extern "gs_ui_property_double" fn property_double(c: Context, name: const u8[:], min: f64,
                                                  value: f64&, max: f64, step: f64,
                                                  inc_per_pixel: f32) -> bool;
extern "gs_ui_propertyi" fn propertyi(c: Context, name: const u8[:], min: i64, value: i64, max: i64,
                                      step: i64, inc_per_pixel: f32) -> i64;
extern "gs_ui_propertyf" fn propertyf(c: Context, name: const u8[:], min: f32, value: f32, max: f32,
                                      step: f32, inc_per_pixel: f32) -> f32;
extern "gs_ui_propertyd" fn propertyd(c: Context, name: const u8[:], min: f64, value: f64, max: f64,
                                      step: f64, inc_per_pixel: f32) -> f64;

// --- text editing ------------------------------------------------------------

// A text field over the first `len` bytes of `buffer`, which holds up to
// buffer.len - 1: returns EDIT_* states.
extern "gs_ui_edit_string" fn edit_string(c: Context, flags: i64, buffer: u8[:], len: i64&,
                                          filter: i64) -> i64;
// A text field over `text`, a limited array (u8[..k], u8[..]) holding at
// most its capacity.
fn edit_string<T>(c: Context, flags: i64, text: T&, filter: i64) -> i64 {
    var buffer: u8[>..] = [];
    buffer.append(text);
    buffer.resize(text.cap + 1, 0);
    var len = text.len;
    let r = edit_string(c, flags, buffer, len, filter);
    text.resize(0);
    text.append(buffer[..len]);
    r
}
// A text field over a text editor, which keeps its undo history.
extern "gs_ui_edit_buffer" fn edit_buffer(c: Context, flags: i64, edit: TextEdit,
                                          filter: i64) -> i64;
// The next edit in the window gets the keyboard, or no edit does.
extern "gs_ui_edit_focus" fn edit_focus(c: Context, flags: i64);
extern "gs_ui_edit_unfocus" fn edit_unfocus(c: Context);
// Whether a FILTER_* lets a character through.
extern "gs_ui_filter_accepts" fn filter_accepts(filter: i64, rune: u32) -> bool;

// A text editor: text, a cursor, a selection and undo, for edit_buffer.
extern "gs_ui_create_text_edit" fn create_text_edit() -> TextEdit;
extern "gs_ui_destroy_text_edit" fn destroy(e: TextEdit);
extern "gs_ui_text_edit_is_valid" fn is_valid(e: TextEdit) -> bool;
extern "gs_ui_text_edit_text" fn text_into(e: TextEdit, out: u8[:]) -> i64;
fn text(e: TextEdit) -> u8[>..] {
    var out: u8[>..] = [];
    let n = text_into(e, out);
    out.resize(n, 0);
    text_into(e, out);
    return out;
}
// Replaces the text, with the cursor at its start and no undo.
extern "gs_ui_text_edit_set_text" fn set_text(e: TextEdit, text: const u8[:]);
extern "gs_ui_text_edit_state_of" fn state(e: TextEdit) -> TextEditState;
extern "gs_ui_text_edit_set_cursor" fn set_cursor(e: TextEdit, cursor: i64, select_start: i64,
                                                  select_end: i64);
// TEXT_EDIT_MODE_*: what typing does. A new editor only views.
extern "gs_ui_text_edit_set_mode" fn set_mode(e: TextEdit, mode: i64);
// Nuklear's editing operations: typing, deleting characters, the selection,
// cut and paste, undo and redo.
extern "gs_ui_textedit_text" fn textedit_text(e: TextEdit, text: const u8[:]);
extern "gs_ui_textedit_delete" fn textedit_delete(e: TextEdit, at: i64, len: i64);
extern "gs_ui_textedit_delete_selection" fn textedit_delete_selection(e: TextEdit);
extern "gs_ui_textedit_select_all" fn textedit_select_all(e: TextEdit);
extern "gs_ui_textedit_cut" fn textedit_cut(e: TextEdit) -> bool;
extern "gs_ui_textedit_paste" fn textedit_paste(e: TextEdit, text: const u8[:]) -> bool;
extern "gs_ui_textedit_undo" fn textedit_undo(e: TextEdit);
extern "gs_ui_textedit_redo" fn textedit_redo(e: TextEdit);

// --- charts ------------------------------------------------------------------

// A chart of `count` values from min to max, pushed one by one: chart_end
// follows when true. More slots add more lines or columns, pushed to by
// index; chart_push says whether the mouse hovers or clicked a value.
extern "gs_ui_chart_begin" fn chart_begin(c: Context, chart_type: i64, count: i64, min: f32,
                                          max: f32) -> bool;
extern "gs_ui_chart_begin_colored" fn chart_begin_colored(c: Context, chart_type: i64, color: Color,
                                                          active: Color, count: i64, min: f32,
                                                          max: f32) -> bool;
extern "gs_ui_chart_add_slot" fn chart_add_slot(c: Context, chart_type: i64, count: i64, min: f32,
                                                max: f32);
extern "gs_ui_chart_add_slot_colored" fn chart_add_slot_colored(c: Context, chart_type: i64,
                                                                color: Color, active: Color,
                                                                count: i64, min: f32, max: f32);
extern "gs_ui_chart_push" fn chart_push(c: Context, value: f32) -> i64;
extern "gs_ui_chart_push_slot" fn chart_push_slot(c: Context, value: f32, slot: i64) -> i64;
extern "gs_ui_chart_end" fn chart_end(c: Context);
// A whole chart of `values`, scaled to their range.
extern "gs_ui_plot" fn plot(c: Context, chart_type: i64, values: const f32[:]);

// --- popups ------------------------------------------------------------------

// A window over the window, at `rect` relative to it: popup_end follows when
// true, and popup_close closes it. Popups do not open inside popups.
extern "gs_ui_popup_begin" fn popup_begin(c: Context, popup_type: i64, title: const u8[:],
                                          flags: i64, rect: Rect) -> bool;
extern "gs_ui_popup_close" fn popup_close(c: Context);
extern "gs_ui_popup_end" fn popup_end(c: Context);
extern "gs_ui_popup_get_scroll" fn popup_get_scroll(c: Context) -> Scroll;
extern "gs_ui_popup_set_scroll" fn popup_set_scroll(c: Context, x: i64, y: i64);

// --- combo boxes -------------------------------------------------------------

// A combo box of `count` items separated by `separator` in one string: the
// index of the one selected now.
extern "gs_ui_combo_separator" fn combo_separator(c: Context, items: const u8[:], separator: i64,
                                                  selected: i64, count: i64, item_height: i64,
                                                  size: float2) -> i64;
// The same, changing `selected`, and saying whether it did.
extern "gs_ui_combobox_separator" fn combobox_separator(c: Context, items: const u8[:],
                                                        separator: i64, selected: i64&,
                                                        count: i64, item_height: i64,
                                                        size: float2) -> bool;
// Items separated by zero bytes.
fn combo_string(c: Context, items: const u8[:], selected: i64, count: i64, item_height: i64,
                size: float2) -> i64 {
    combo_separator(c, items, 0, selected, count, item_height, size)
}
fn combobox_string(c: Context, items: const u8[:], selected: i64&, count: i64, item_height: i64,
                   size: float2) -> bool {
    combobox_separator(c, items, 0, selected, count, item_height, size)
}
// Items as an array of strings.
fn combo(c: Context, items: const u8[:][:], selected: i64, item_height: i64, size: float2) -> i64 {
    var joined: u8[>..] = [];
    for it, i in items {
        if i > 0 { joined.push(0); }
        joined.append(it);
    }
    combo_separator(c, joined, 0, selected, items.len, item_height, size)
}
fn combobox(c: Context, items: const u8[:][:], selected: i64&, item_height: i64,
            size: float2) -> bool {
    var joined: u8[>..] = [];
    for it, i in items {
        if i > 0 { joined.push(0); }
        joined.append(it);
    }
    combobox_separator(c, joined, 0, selected, items.len, item_height, size)
}
// A combo box of the program's own: what the closed box shows, and while
// open (true) combo items or any widgets, then combo_end.
extern "gs_ui_combo_begin_label" fn combo_begin_label(c: Context, selected: const u8[:],
                                                      size: float2) -> bool;
extern "gs_ui_combo_begin_color" fn combo_begin_color(c: Context, color: Color,
                                                      size: float2) -> bool;
extern "gs_ui_combo_begin_symbol" fn combo_begin_symbol(c: Context, symbol: i64,
                                                        size: float2) -> bool;
extern "gs_ui_combo_begin_symbol_label" fn combo_begin_symbol_label(c: Context,
                                                                    selected: const u8[:],
                                                                    symbol: i64,
                                                                    size: float2) -> bool;
extern "gs_ui_combo_begin_image" fn combo_begin_image(c: Context, img: Image, size: float2) -> bool;
extern "gs_ui_combo_begin_image_label" fn combo_begin_image_label(c: Context,
                                                                  selected: const u8[:],
                                                                  img: Image,
                                                                  size: float2) -> bool;
// True when clicked, which closes the combo box.
extern "gs_ui_combo_item_label" fn combo_item_label(c: Context, text: const u8[:],
                                                    align: i64) -> bool;
extern "gs_ui_combo_item_image_label" fn combo_item_image_label(c: Context, img: Image,
                                                                text: const u8[:],
                                                                align: i64) -> bool;
extern "gs_ui_combo_item_symbol_label" fn combo_item_symbol_label(c: Context, symbol: i64,
                                                                  text: const u8[:],
                                                                  align: i64) -> bool;
extern "gs_ui_combo_close" fn combo_close(c: Context);
extern "gs_ui_combo_end" fn combo_end(c: Context);

// --- contextual menus --------------------------------------------------------

// A menu opened by right-clicking `trigger_bounds`: contextual_end follows
// when true.
extern "gs_ui_contextual_begin" fn contextual_begin(c: Context, flags: i64, size: float2,
                                                    trigger_bounds: Rect) -> bool;
extern "gs_ui_contextual_item_label" fn contextual_item_label(c: Context, text: const u8[:],
                                                              align: i64) -> bool;
extern "gs_ui_contextual_item_image_label" fn contextual_item_image_label(c: Context, img: Image,
                                                                          text: const u8[:],
                                                                          align: i64) -> bool;
extern "gs_ui_contextual_item_symbol_label" fn contextual_item_symbol_label(c: Context,
                                                                            symbol: i64,
                                                                            text: const u8[:],
                                                                            align: i64) -> bool;
extern "gs_ui_contextual_close" fn contextual_close(c: Context);
extern "gs_ui_contextual_end" fn contextual_end(c: Context);

// --- tooltips ----------------------------------------------------------------

// Text by the pointer this frame, usually after widget_is_hovered.
extern "gs_ui_tooltip" fn tooltip(c: Context, text: const u8[:]);
extern "gs_ui_tooltip_offset" fn tooltip_offset(c: Context, text: const u8[:], position: i64,
                                                offset: float2);
// A tooltip of widgets, `width` wide: tooltip_end follows when true.
extern "gs_ui_tooltip_begin" fn tooltip_begin(c: Context, width: f32) -> bool;
extern "gs_ui_tooltip_begin_offset" fn tooltip_begin_offset(c: Context, width: f32, position: i64,
                                                            offset: float2) -> bool;
extern "gs_ui_tooltip_end" fn tooltip_end(c: Context);
// A tooltip while the mouse is over `bounds`; the _delay ones once it has
// stayed still there for the style's tooltip_delay, counting in `timer`,
// the _clicked one not after a click.
extern "gs_ui_do_tooltip" fn do_tooltip(c: Context, text: const u8[:], bounds: Rect);
extern "gs_ui_do_tooltip_delay" fn do_tooltip_delay(c: Context, text: const u8[:], bounds: Rect,
                                                    timer: f32&);
extern "gs_ui_do_tooltip_delay_clicked" fn do_tooltip_delay_clicked(c: Context, text: const u8[:],
                                                                    bounds: Rect, timer: f32&,
                                                                    clicked: bool&);

// --- menus -------------------------------------------------------------------

// A menu bar, first in its window: menus go in it like widgets in a row.
extern "gs_ui_menubar_begin" fn menubar_begin(c: Context);
extern "gs_ui_menubar_end" fn menubar_end(c: Context);
// A menu that opens `size` large when clicked: menu_end follows when true.
extern "gs_ui_menu_begin_label" fn menu_begin_label(c: Context, text: const u8[:], align: i64,
                                                    size: float2) -> bool;
extern "gs_ui_menu_begin_image" fn menu_begin_image(c: Context, id: const u8[:], img: Image,
                                                    size: float2) -> bool;
extern "gs_ui_menu_begin_image_label" fn menu_begin_image_label(c: Context, text: const u8[:],
                                                                align: i64, img: Image,
                                                                size: float2) -> bool;
extern "gs_ui_menu_begin_symbol" fn menu_begin_symbol(c: Context, id: const u8[:], symbol: i64,
                                                      size: float2) -> bool;
extern "gs_ui_menu_begin_symbol_label" fn menu_begin_symbol_label(c: Context, text: const u8[:],
                                                                  align: i64, symbol: i64,
                                                                  size: float2) -> bool;
// True when clicked, which closes the menu.
extern "gs_ui_menu_item_label" fn menu_item_label(c: Context, text: const u8[:],
                                                  align: i64) -> bool;
extern "gs_ui_menu_item_image_label" fn menu_item_image_label(c: Context, img: Image,
                                                              text: const u8[:],
                                                              align: i64) -> bool;
extern "gs_ui_menu_item_symbol_label" fn menu_item_symbol_label(c: Context, symbol: i64,
                                                                text: const u8[:],
                                                                align: i64) -> bool;
extern "gs_ui_menu_close" fn menu_close(c: Context);
extern "gs_ui_menu_end" fn menu_end(c: Context);

// --- style -------------------------------------------------------------------

// The whole style, changed as a value and set back: `var s = ui::style(ctx);
// s.window.rounding = 6.0; ui::set_style(ctx, s);`.
extern "gs_ui_style_of" fn style(c: Context) -> Style;
extern "gs_ui_set_style" fn set_style(c: Context, s: const Style&);
// A style for a while: push_style sets it, pop_style puts back the one
// before.
extern "gs_ui_push_style" fn push_style(c: Context, s: const Style&) -> bool;
extern "gs_ui_pop_style" fn pop_style(c: Context) -> bool;
extern "gs_ui_style_default" fn style_default(c: Context);
// A style from a color per COLOR_*, as Nuklear builds its own from its
// default table.
extern "gs_ui_style_from_table" fn style_from_table(c: Context, table: const Color[:]);
extern "gs_ui_default_color_table" fn default_color_table_into(out: Color[:]) -> i64;
fn default_color_table() -> Color[>..] {
    var out: Color[>..] = [];
    let n = default_color_table_into(out);
    out.resize(n, Color { 0, 0, 0, 0 });
    default_color_table_into(out);
    return out;
}
extern "gs_ui_style_get_color_by_name" fn style_get_color_by_name_into(color: i64,
                                                                       out: u8[:]) -> i64;
// Nuklear's name for a COLOR_*: "NK_COLOR_TEXT".
fn style_get_color_by_name(color: i64) -> u8[>..] {
    var out: u8[>..] = [];
    let n = style_get_color_by_name_into(color, out);
    out.resize(n, 0);
    style_get_color_by_name_into(color, out);
    return out;
}
extern "gs_ui_style_set_font" fn style_set_font(c: Context, font: Font);
extern "gs_ui_style_push_font" fn style_push_font(c: Context, font: Font) -> bool;
extern "gs_ui_style_pop_font" fn style_pop_font(c: Context) -> bool;
// Software cursors, drawn by Nuklear at the pointer once loaded: one, or
// one per CURSOR_*.
extern "gs_ui_style_load_cursor" fn style_load_cursor(c: Context, cursor: i64, image: Cursor);
extern "gs_ui_style_load_all_cursors" fn style_load_all_cursors(c: Context,
                                                                cursors: const Cursor[:]);
extern "gs_ui_style_set_cursor" fn style_set_cursor(c: Context, cursor: i64) -> bool;
extern "gs_ui_style_show_cursor" fn style_show_cursor(c: Context);
extern "gs_ui_style_hide_cursor" fn style_hide_cursor(c: Context);

fn style_item_color(color: Color) -> StyleItem {
    StyleItem { kind: STYLE_ITEM_COLOR, color: color }
}
fn style_item_image(img: Image) -> StyleItem { StyleItem { kind: STYLE_ITEM_IMAGE, img: img } }
fn style_item_nine_slice(slice: NineSlice) -> StyleItem {
    StyleItem { kind: STYLE_ITEM_NINE_SLICE, img: slice.img, l: slice.l, t: slice.t, r: slice.r,
                b: slice.b }
}
// Nothing drawn: a transparent color.
fn style_item_hide() -> StyleItem { style_item_color(Color { 0, 0, 0, 0 }) }

// --- colors ------------------------------------------------------------------

// Channels from 0 to 255, or floats from 0 to 1; hsv in the same ranges.
extern "gs_ui_rgb" fn rgb(r: i64, g: i64, b: i64) -> Color;
extern "gs_ui_rgba" fn rgba(r: i64, g: i64, b: i64, a: i64) -> Color;
// 0xAABBGGRR.
extern "gs_ui_rgba_u32" fn rgba_u32(rgba: u32) -> Color;
extern "gs_ui_rgb_f" fn rgb_f(r: f32, g: f32, b: f32) -> Color;
extern "gs_ui_rgba_f" fn rgba_f(r: f32, g: f32, b: f32, a: f32) -> Color;
extern "gs_ui_rgb_cf" fn rgb_cf(c: Colorf) -> Color;
extern "gs_ui_rgba_cf" fn rgba_cf(c: Colorf) -> Color;
// "#RRGGBB" and "#RRGGBBAA", the # optional.
extern "gs_ui_rgb_hex" fn rgb_hex(text: const u8[:]) -> Color;
extern "gs_ui_rgba_hex" fn rgba_hex(text: const u8[:]) -> Color;
// The color darkened by `factor`, from 0 to 1.
extern "gs_ui_rgb_factor" fn rgb_factor(c: Color, factor: f32) -> Color;
extern "gs_ui_hsv" fn hsv(h: i64, s: i64, v: i64) -> Color;
extern "gs_ui_hsva" fn hsva(h: i64, s: i64, v: i64, a: i64) -> Color;
extern "gs_ui_hsv_f" fn hsv_f(h: f32, s: f32, v: f32) -> Color;
extern "gs_ui_hsva_f" fn hsva_f(h: f32, s: f32, v: f32, a: f32) -> Color;
extern "gs_ui_hsva_colorf" fn hsva_colorf(h: f32, s: f32, v: f32, a: f32) -> Colorf;
// Hue, saturation, value and alpha, from 0 to 1.
extern "gs_ui_colorf_hsva_f" fn colorf_hsva_f(c: Colorf) -> float4;
extern "gs_ui_color_cf" fn color_cf(c: Color) -> Colorf;
extern "gs_ui_color_u32" fn color_u32(c: Color) -> u32;
extern "gs_ui_color_hex_rgba" fn color_hex_rgba_into(c: Color, out: u8[:]) -> i64;
extern "gs_ui_color_hex_rgb" fn color_hex_rgb_into(c: Color, out: u8[:]) -> i64;
// "RRGGBBAA" and "RRGGBB".
fn color_hex_rgba(c: Color) -> u8[>..] {
    var out: u8[>..] = [];
    out.resize(8, 0);
    color_hex_rgba_into(c, out);
    return out;
}
fn color_hex_rgb(c: Color) -> u8[>..] {
    var out: u8[>..] = [];
    out.resize(6, 0);
    color_hex_rgb_into(c, out);
    return out;
}
extern "gs_ui_color_hsv_f" fn color_hsv_f(c: Color) -> float3;
extern "gs_ui_color_hsva_f" fn color_hsva_f(c: Color) -> float4;

// --- images and rectangles ---------------------------------------------------

// An image of a whole texture, by its handle's id: gfx::Texture's id for
// render().
fn image_id(texture: u32) -> Image { Image { texture: texture } }
fn texture_image(t: gfx::Texture) -> Image { image_id(t.id) }
// Part of a texture w x h, `region` in pixels.
fn subimage_id(texture: u32, w: u16, h: u16, region: Rect) -> Image {
    Image { texture: texture, w: w, h: h,
            region: [region.x as! u16, region.y as! u16, region.w as! u16, region.h as! u16] }
}
fn image_is_subimage(img: Image) -> bool { !(img.w == 0 && img.h == 0) }
// Nine parts are cut from a texture whose size the image gives: a
// nine_slice_id, without one, draws the whole texture in each part.
fn nine_slice_id(texture: u32, l: u16, t: u16, r: u16, b: u16) -> NineSlice {
    NineSlice { img: image_id(texture), l: l, t: t, r: r, b: b }
}
fn sub9slice_id(texture: u32, w: u16, h: u16, region: Rect, l: u16, t: u16, r: u16,
                b: u16) -> NineSlice {
    NineSlice { img: subimage_id(texture, w, h, region), l: l, t: t, r: r, b: b }
}
fn nine_slice_is_sub9slice(s: NineSlice) -> bool { image_is_subimage(s.img) }

fn rect(x: f32, y: f32, w: f32, h: f32) -> Rect { Rect { x, y, w, h } }
fn rect(x: i64, y: i64, w: i64, h: i64) -> Rect { Rect { x as f32, y as f32, w as f32, h as f32 } }
fn recta(pos: float2, size: float2) -> Rect { Rect { pos.x, pos.y, size.x, size.y } }
fn rect_pos(r: Rect) -> float2 { float2 { r.x, r.y } }
fn rect_size(r: Rect) -> float2 { float2 { r.w, r.h } }
// Nuklear's rectangle for "anywhere", as its clipping uses.
fn get_null_rect() -> Rect { Rect { -8192.0, -8192.0, 16384.0, 16384.0 } }

// --- drawing on the window ---------------------------------------------------

// Shapes, images and text drawn straight onto the window being built, in
// screen coordinates, clipped to push_scissor's rectangle: the way to draw
// a widget of the program's own at what widget() returned.
extern "gs_ui_stroke_line" fn stroke_line(c: Context, x0: f32, y0: f32, x1: f32, y1: f32,
                                          line_thickness: f32, color: Color);
extern "gs_ui_stroke_curve" fn stroke_curve(c: Context, ax: f32, ay: f32, ctrl0x: f32, ctrl0y: f32,
                                            ctrl1x: f32, ctrl1y: f32, bx: f32, by: f32,
                                            line_thickness: f32, color: Color);
extern "gs_ui_stroke_rect" fn stroke_rect(c: Context, r: Rect, rounding: f32, line_thickness: f32,
                                          color: Color);
extern "gs_ui_stroke_circle" fn stroke_circle(c: Context, r: Rect, line_thickness: f32,
                                              color: Color);
// Angles in radians.
extern "gs_ui_stroke_arc" fn stroke_arc(c: Context, cx: f32, cy: f32, radius: f32, a_min: f32,
                                        a_max: f32, line_thickness: f32, color: Color);
extern "gs_ui_stroke_triangle" fn stroke_triangle(c: Context, x0: f32, y0: f32, x1: f32, y1: f32,
                                                  x2: f32, y2: f32, line_thickness: f32,
                                                  color: Color);
extern "gs_ui_stroke_polyline" fn stroke_polyline(c: Context, points: const float2[:],
                                                  line_thickness: f32, color: Color);
extern "gs_ui_stroke_polygon" fn stroke_polygon(c: Context, points: const float2[:],
                                                line_thickness: f32, color: Color);
extern "gs_ui_fill_rect" fn fill_rect(c: Context, r: Rect, rounding: f32, color: Color);
extern "gs_ui_fill_rect_multi_color" fn fill_rect_multi_color(c: Context, r: Rect, left: Color,
                                                              top: Color, right: Color,
                                                              bottom: Color);
extern "gs_ui_fill_circle" fn fill_circle(c: Context, r: Rect, color: Color);
extern "gs_ui_fill_arc" fn fill_arc(c: Context, cx: f32, cy: f32, radius: f32, a_min: f32,
                                    a_max: f32, color: Color);
extern "gs_ui_fill_triangle" fn fill_triangle(c: Context, x0: f32, y0: f32, x1: f32, y1: f32,
                                              x2: f32, y2: f32, color: Color);
extern "gs_ui_fill_polygon" fn fill_polygon(c: Context, points: const float2[:], color: Color);
extern "gs_ui_draw_image" fn draw_image(c: Context, r: Rect, img: Image, color: Color);
extern "gs_ui_draw_nine_slice" fn draw_nine_slice(c: Context, r: Rect, slice: NineSlice,
                                                  color: Color);
extern "gs_ui_draw_text" fn draw_text(c: Context, r: Rect, text: const u8[:], font: Font,
                                      background: Color, foreground: Color);
extern "gs_ui_push_scissor" fn push_scissor(c: Context, r: Rect);

// --- utilities ---------------------------------------------------------------

extern "gs_ui_murmur_hash" fn murmur_hash(key: const u8[:], seed: u32) -> u32;
extern "gs_ui_triangle_from_direction" fn triangle_from_direction_into(r: Rect, pad_x: f32,
                                                                       pad_y: f32, direction: i64,
                                                                       out: float2[:]);
// The triangle pointing `direction` (UP, RIGHT, DOWN, LEFT) in `r`.
fn triangle_from_direction(r: Rect, pad_x: f32, pad_y: f32, direction: i64) -> float2[3] {
    var out: float2[3] = [float2 { 0.0, 0.0 }; 3];
    triangle_from_direction_into(r, pad_x, pad_y, direction, out);
    out
}
// Whether `text` matches Nuklear's small patterns: ^ and $ anchor, . is any
// character, c* any run of c.
extern "gs_ui_strfilter" fn strfilter(text: const u8[:], regexp: const u8[:]) -> bool;
extern "gs_ui_strmatch_fuzzy_text" fn strmatch_fuzzy_text_into(text: const u8[:],
                                                               pattern: const u8[:],
                                                               score: i64&) -> bool;
// Whether `pattern`'s characters appear in `text` in order, and how well
// (higher is better), for a search box.
fn strmatch_fuzzy_text(text: const u8[:], pattern: const u8[:]) -> bool, i64 {
    var score = 0;
    let found = strmatch_fuzzy_text_into(text, pattern, score);
    return found, score;
}

// --- through gfx -------------------------------------------------------------

// What drawing through gfx keeps between frames, shared by every context:
// made the first time, and again after gfx is opened again (its handles
// died with gfx::close).
struct Renderer {
    opened: i64,                    // gfx::open_count() when made; 0: never made
    pipeline: gfx::Pipeline,
    sampler: gfx::Sampler,
    vertex_buffer: gfx::Buffer,
    vertex_capacity: i64,           // bytes
    index_buffer: gfx::Buffer,
    index_capacity: i64,
    text_input: bool,               // gfx's text input turned on for input_from_gfx
    // Scancodes of the keys input_from_gfx maps, by the names gfx has for them.
    keys: i64[16],
}

var renderer = Renderer { 0, gfx::Pipeline { 0 }, gfx::Sampler { 0 }, gfx::Buffer { 0 }, 0,
                          gfx::Buffer { 0 }, 0, false, [0; 16] };

fn make_renderer() {
    let vs = embed_shader("vert", """
        layout(location = 0) in vec2 a_pos;
        layout(location = 1) in vec2 a_uv;
        layout(location = 2) in vec4 a_color;
        layout(set = 1, binding = 0) uniform Target { vec4 size; };
        layout(location = 0) out vec2 v_uv;
        layout(location = 1) out vec4 v_color;
        void main() {
            v_uv = a_uv;
            v_color = a_color;
            vec2 p = a_pos / size.xy;
            gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        }
        """);
    let fs = embed_shader("frag", """
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(location = 0) in vec2 v_uv;
        layout(location = 1) in vec4 v_color;
        layout(location = 0) out vec4 result;
        void main() { result = v_color * texture(tex, v_uv); }
        """);
    var desc = gfx::PipelineDesc { blend: gfx::BLEND_ALPHA };
    desc.formats[2] = gfx::VERTEX_UBYTE4_NORM;
    renderer.pipeline = gfx::pipeline(vs, fs, desc);
    renderer.sampler = gfx::sampler(gfx::LINEAR, gfx::CLAMP);
    renderer.vertex_buffer = gfx::Buffer { 0 };
    renderer.vertex_capacity = 0;
    renderer.index_buffer = gfx::Buffer { 0 };
    renderer.index_capacity = 0;
    renderer.text_input = false;
    renderer.opened = gfx::open_count();
    // The atlases' textures died with the gfx they were made in.
    forget_textures();
    for i in renderer_textures.len { renderer_textures[i] = 0; }
}

// A buffer of at least `bytes`, grown to the next power of two.
fn ensure_buffer(b: gfx::Buffer&, capacity: i64&, usage: i32, bytes: i64) {
    if bytes <= capacity { return; }
    if b.id != 0 { gfx::release(b); }
    capacity = next_pow2(max(bytes, 4096));
    let none: u8[>..] = [];
    b = gfx::buffer(usage, capacity, none);
}

// The gfx textures upload_atlases made, which are released with their
// atlases; a 0 is a free slot. A global array only grows: a program's
// global strings could be views of it.
var renderer_textures: u32[>..] = [];

// Uploads the image of every baked atlas no texture holds yet, and releases
// the textures of those destroyed.
fn upload_atlases() {
    for t in released_textures() {
        let i = position(renderer_textures, t);
        if i >= 0 {
            gfx::release(gfx::Texture { t });
            renderer_textures[i] = 0;
        }
    }
    loop {
        let a = atlas_without_texture();
        if a.id == 0 { break; }
        let size = atlas_size(a);
        let t = gfx::texture2d(size.x, size.y);
        gfx::update_texture(t, atlas_pixels(a));
        set_texture(a, t.id);
        let free = position(renderer_textures, 0);
        if free >= 0 { renderer_textures[free] = t.id; } else { renderer_textures.push(t.id); }
    }
}

// Draws the context's frame over `target`, a gfx texture of COLOR_TARGET
// usage, with alpha blending; not inside a pass. The frame's windows must
// have ended.
fn render(c: Context, target: gfx::Texture, config: ConvertConfig) {
    check();
    if renderer.opened != gfx::open_count() { make_renderer(); }
    upload_atlases();
    convert(c, config);
    let draws = draw_commands(c);
    if draws.len == 0 { return; }
    let verts = vertices(c);
    let idx = indices(c);
    ensure_buffer(renderer.vertex_buffer, renderer.vertex_capacity, gfx::BUFFER_VERTEX,
                  bytes_of(verts).len);
    ensure_buffer(renderer.index_buffer, renderer.index_capacity, gfx::BUFFER_INDEX,
                  bytes_of(idx).len);
    gfx::update_buffer(renderer.vertex_buffer, bytes_of(verts));
    gfx::update_buffer(renderer.index_buffer, bytes_of(idx));
    let info = gfx::texture_info(target);
    var pass = gfx::PassDesc { clear_color: false, clear_depth: false };
    pass.color[0] = target;
    gfx::begin_pass(pass);
    gfx::bind(renderer.pipeline);
    gfx::bind_vertex_buffer(renderer.vertex_buffer);
    gfx::bind_index_buffer(renderer.index_buffer, 4);
    gfx::uniforms(gfx::VERTEX, 0, float4 { info.width as f32, info.height as f32, 0.0, 0.0 });
    var first = 0;
    for d in draws {
        // Scissors in whole pixels, inside the target.
        let x0 = clamp(floor(d.clip_rect.x) as! i64, 0, info.width as i64);
        let y0 = clamp(floor(d.clip_rect.y) as! i64, 0, info.height as i64);
        let x1 = clamp(ceil(d.clip_rect.x + d.clip_rect.w) as! i64, 0, info.width as i64);
        let y1 = clamp(ceil(d.clip_rect.y + d.clip_rect.h) as! i64, 0, info.height as i64);
        if x1 > x0 && y1 > y0 {
            gfx::scissor(x0, y0, x1 - x0, y1 - y0);
            gfx::bind_texture(gfx::FRAGMENT, 0, gfx::Texture { d.texture }, renderer.sampler);
            gfx::draw_indexed(d.elem_count as i64, 1, first, 0, 0);
        }
        first += d.elem_count as i64;
    }
    gfx::end_pass();
}
fn render(c: Context, target: gfx::Texture) { render(c, target, ConvertConfig {}) }
// Over the screen.
fn render(c: Context) { render(c, gfx::screen(), ConvertConfig {}) }

// The scancode of one of the keys input_from_gfx maps, by the name gfx has
// for it.
fn gfx_key(i: i64) -> i64 {
    let names: const u8[:][16] = ["Left Shift", "Right Shift", "Left Ctrl", "Right Ctrl", "Delete",
                                  "Return", "Keypad Enter", "Tab", "Backspace", "Home", "End",
                                  "PageDown", "PageUp", "Up", "Down", "Escape"];
    if renderer.keys[i] == 0 { renderer.keys[i] = gfx::scancode(names[i]); }
    renderer.keys[i]
}

// Nuklear's keys for a gfx key event: editing keys by name, shortcuts by
// the character they type, with Ctrl (or the Command key).
fn input_gfx_key(c: Context, e: gfx::Event) {
    let cmd = (e.mods & (gfx::MOD_CTRL | gfx::MOD_GUI)) != 0;
    let shift = (e.mods & gfx::MOD_SHIFT) != 0;
    let sc = e.scancode as i64;
    if sc == gfx_key(0) || sc == gfx_key(1) { input_key(c, KEY_SHIFT, e.down); }
    else if sc == gfx_key(2) || sc == gfx_key(3) { input_key(c, KEY_CTRL, e.down); }
    else if sc == gfx_key(4) { input_key(c, KEY_DEL, e.down); }
    else if sc == gfx_key(5) || sc == gfx_key(6) { input_key(c, KEY_ENTER, e.down); }
    else if sc == gfx_key(7) { input_key(c, KEY_TAB, e.down); }
    else if sc == gfx_key(8) { input_key(c, KEY_BACKSPACE, e.down); }
    else if sc == gfx_key(9) {
        input_key(c, KEY_TEXT_START, e.down);
        input_key(c, KEY_SCROLL_START, e.down);
    } else if sc == gfx_key(10) {
        input_key(c, KEY_TEXT_END, e.down);
        input_key(c, KEY_SCROLL_END, e.down);
    } else if sc == gfx_key(11) { input_key(c, KEY_SCROLL_DOWN, e.down); }
    else if sc == gfx_key(12) { input_key(c, KEY_SCROLL_UP, e.down); }
    else if sc == gfx_key(13) { input_key(c, KEY_UP, e.down); }
    else if sc == gfx_key(14) { input_key(c, KEY_DOWN, e.down); }
    else if sc == gfx_key(15) { input_key(c, KEY_TEXT_RESET_MODE, e.down); }
    else if sc == gfx::scancode("Left") {
        input_key(c, if cmd { KEY_TEXT_WORD_LEFT } else { KEY_LEFT }, e.down);
    } else if sc == gfx::scancode("Right") {
        input_key(c, if cmd { KEY_TEXT_WORD_RIGHT } else { KEY_RIGHT }, e.down);
    } else if e.keycode == 'a' { input_key(c, KEY_TEXT_SELECT_ALL, e.down && cmd); }
    else if e.keycode == 'z' {
        input_key(c, if shift { KEY_TEXT_REDO } else { KEY_TEXT_UNDO }, e.down && cmd);
    } else if e.keycode == 'y' { input_key(c, KEY_TEXT_REDO, e.down && cmd); }
    else if e.keycode == 'c' { input_key(c, KEY_COPY, e.down && cmd); }
    else if e.keycode == 'x' { input_key(c, KEY_CUT, e.down && cmd); }
    else if e.keycode == 'v' {
        // What is pasted is the system clipboard's when the keys go down.
        if e.down && cmd { set_clipboard(c, gfx::clipboard()); }
        input_key(c, KEY_PASTE, e.down && cmd);
    } else {
        // Nuklear has no use for the other keys: their text comes as text.
    }
}

// Nuklear's input for a frame from gfx's: the last frame's events in order
// (keys, typed text, the mouse), its duration, and the clipboard both ways.
// Call it once a frame, after gfx::frame() and before any window.
fn input_from_gfx(c: Context) {
    if renderer.opened != gfx::open_count() { make_renderer(); }
    if !renderer.text_input {
        gfx::text_input(true);
        renderer.text_input = true;
    }
    // What a text edit copied last frame goes to the system clipboard.
    let text, copied_some = copied(c);
    if copied_some { gfx::set_clipboard(text); }
    input_begin(c);
    for e in gfx::events() {
        let x = floor(e.x) as! i64;
        let y = floor(e.y) as! i64;
        if e.kind == gfx::EVENT_MOUSE_MOTION {
            input_motion(c, x, y);
        } else if e.kind == gfx::EVENT_MOUSE_BUTTON {
            let b = match e.button {
                1 => BUTTON_LEFT,
                2 => BUTTON_MIDDLE,
                3 => BUTTON_RIGHT,
                4 => BUTTON_X1,
                5 => BUTTON_X2,
                _ => -1,
            };
            if b == BUTTON_LEFT && e.clicks > 1 { input_button(c, BUTTON_DOUBLE, x, y, e.down); }
            if b >= 0 { input_button(c, b, x, y, e.down); }
        } else if e.kind == gfx::EVENT_MOUSE_WHEEL {
            input_scroll(c, float2 { e.x, e.y });
        } else if e.kind == gfx::EVENT_TEXT {
            input_unicode(c, e.codepoint);
        } else if e.kind == gfx::EVENT_KEY {
            input_gfx_key(c, e);
        }
    }
    input_end(c);
    set_delta_time(c, gfx::delta_time() as! f32);
}
