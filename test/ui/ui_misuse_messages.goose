// What the ui layer says about uses it refuses: each misuse below is
// skipped, its reason left in ui::error(), and nothing checks until the end
// -- the program uses the _raw forms of the calls that check -- so every
// message can be seen. They name the call and what was wrong with it.
import std;
import vec;
import ui;

fn said(what: const u8[:]) { print(what, ": ", ui::error()); }

fn main() {
    // --- handles ---
    ui::clear(ui::Context { 0 });
    said("no context");
    let gone = ui::create(13.0);
    ui::destroy_raw(gone);
    ui::clear(gone);
    said("a destroyed context");
    let edit = ui::create_text_edit();
    ui::destroy(edit);
    ui::set_text(edit, "x");
    said("a destroyed text editor");

    // --- fonts ---
    let atlas = ui::create_font_atlas();
    ui::add_default_font(atlas, 0.0);
    said("a font 0 high");
    ui::bake(atlas);
    said("baking no fonts");
    let junk: u8[4] = [1, 2, 3, 4];
    ui::add_font_from_memory(atlas, junk, 13.0);
    said("a font from junk");
    ui::create_from_memory(junk, 13.0);
    said("a context of a font from junk");
    ui::add_default_font(atlas, 13.0, ui::FontConfig { oversample_h: 0 });
    said("no oversampling");
    ui::add_default_font(atlas, 13.0, ui::FontConfig { merge_mode: true });
    said("merging into nothing");
    ui::add_default_font(atlas, 13.0, ui::FontConfig { ranges: 99 });
    said("a range set that is not one");
    let odd: u32[3] = [32, 127, 200];
    ui::add_default_font(atlas, 13.0, ui::FontConfig {}, odd);
    said("ranges not in pairs");
    let digits: u32[2] = ['0', '9'];
    ui::add_default_font(atlas, 13.0, ui::FontConfig {}, digits);
    said("a fallback outside the ranges");
    print("glyph ranges of nothing: ", ui::glyph_ranges(99).len, " ", ui::error());
    let font = ui::add_default_font(atlas, 13.0);
    ui::create_context(font);
    said("a context before baking");
    ui::bake(atlas);
    ui::bake(atlas);
    said("baking twice");
    ui::add_default_font(atlas, 20.0);
    said("a font after baking");
    let ctx = ui::create_context(font);
    ui::destroy(atlas);
    said("an atlas in use");

    // --- frames and scopes ---
    ui::input_motion(ctx, 1, 1);
    said("input outside input");
    ui::input_begin_raw(ctx);
    ui::input_begin_raw(ctx);
    said("input begun twice");
    ui::begin(ctx, "W", ui::rect(0, 0, 100, 100), 0);
    said("a window during input");
    ui::input_key(ctx, 99, true);
    said("a key that is not one");
    ui::input_button(ctx, 99, 0, 0, true);
    said("a button that is not one");
    ui::input_scroll(ctx, float2 { 0.0, 1.0 / 0.0 });
    said("an endless scroll");
    let long: u8[5] = [0xF0, 0x9F, 0x98, 0x80, 0x80];
    ui::input_glyph(ctx, long);
    said("a glyph of 5 bytes");
    ui::input_end(ctx);
    ui::button_label(ctx, "B");
    said("a widget outside a window");
    ui::begin(ctx, "W", ui::rect(0, 0, 200, 200), 1 << 40);
    said("window flags that are not");
    if ui::begin(ctx, "W", ui::rect(0, 0, 200, 200), 0) {
        ui::button_label(ctx, "B");
        said("a widget before any row");
        ui::layout_row_dynamic(ctx, 20.0, 0);
        said("a row of no columns");
        ui::layout_row_dynamic(ctx, 20.0, 1);
        ui::begin(ctx, "Inner", ui::rect(0, 0, 50, 50), 0);
        said("a window in a window");
        ui::group_end(ctx);
        said("ending no group");
        ui::tree_pop(ctx);
        said("popping no tree");
        ui::popup_end(ctx);
        said("ending no popup");
        if ui::group_begin(ctx, "G", 0) {
            ui::end(ctx);
            said("ending the window inside a group");
            ui::group_end(ctx);
        }
        var value: f32 = 0.5;
        ui::slider_float(ctx, 1.0, value, 1.0, 0.1);
        said("an empty slider range");
        var done = -1;
        ui::progress(ctx, done, 10, true);
        said("progress before its start");
        ui::button_symbol(ctx, 99);
        said("a symbol that is not one");
        var text: u8[4] = [0; 4];
        var len = 4;
        ui::edit_string(ctx, ui::EDIT_FIELD, text, len, ui::FILTER_DEFAULT);
        said("text filling its whole buffer, with no room for Nuklear's zero");
        len = 0;
        ui::edit_string(ctx, ui::EDIT_FIELD, text, len, 42);
        said("a filter that is not one");
        let items: const u8[:][2] = ["a", "b"];
        ui::combo(ctx, items, 5, 20, float2 { 100.0, 100.0 });
        said("a combo item past the end");
        ui::fill_rect(ctx, ui::rect(40000, 0, 10, 10), 0.0, ui::rgb(1, 2, 3));
        said("drawing far away");
        ui::stroke_line(ctx, 0.0, 0.0, 1.0, 1.0, -1.0, ui::rgb(1, 2, 3));
        said("a line of negative thickness");
    }
    ui::end(ctx);
    ui::end(ctx);
    said("ending no window");
    let doubled = ui::ConvertConfig { global_alpha: 2.0 };
    ui::convert_raw(ctx, doubled);
    said("converting at double alpha");
    ui::set_delta_time(ctx, -1.0);
    said("a frame of negative time");
    ui::clear(ctx);

    // --- text editors ---
    let e = ui::create_text_edit();
    ui::set_text(e, "abc");
    ui::set_cursor(e, 5, 0, 0);
    said("a cursor past the text");
    ui::textedit_delete(e, 2, 5);
    said("deleting past the text");
    ui::filter_accepts(7, 'a');
    said("a filter that is not one");
    ui::destroy(e);

    // --- style and colors ---
    let two: ui::Color[2] = [ui::rgb(0, 0, 0), ui::rgb(0, 0, 0)];
    ui::style_from_table(ctx, two);
    said("a table of two colors");
    ui::pop_style(ctx);
    said("popping no style");
    ui::style_pop_font(ctx);
    said("popping no font");
    let few: ui::Cursor[2] = [ui::Cursor { img: ui::image_id(1), size: float2_0, offset: float2_0 }; 2];
    ui::style_load_all_cursors(ctx, few);
    said("two cursors");
    ui::style_set_cursor(ctx, 42);
    said("a cursor that is not one");
    var s = ui::style(ctx);
    s.window.header.align = 7;
    ui::set_style(ctx, s);
    said("a header alignment that is not one");
    ui::rgb_factor(ui::rgb(1, 2, 3), 2.0);
    said("a factor past 1");
    ui::set_scale(ctx, 0.0);
    said("a scale of 0");
    let unbaked = ui::create_font_atlas();
    ui::add_default_font(unbaked, 13.0);
    ui::bake(unbaked, 100.0);
    said("baked 100 times over");
    ui::destroy(unbaked);
    ui::input_begin_raw(ctx);
    ui::set_scale(ctx, 2.0);
    said("a scale set during input");
    ui::input_end(ctx);
    ui::rgb_hex("#12345");
    said("five hexadecimal digits");
    print("a color past the last: '", ui::style_get_color_by_name(ui::COLOR_COUNT), "' ",
          ui::error());

    ui::destroy_raw(ctx);
    ui::destroy(atlas);
    print("misuses counted: ", ui::misuse_count());
}
