// Text editing in the ui module: Nuklear's filters; a text editor driven
// through its own operations (typing, the cursor and selection, deleting,
// cut, paste, undo and redo); text fields over a byte buffer and over a
// limited array -- clicked into, typed into each way input takes text,
// committed with Enter, left with a click elsewhere, focused and unfocused
// by the program, their flags, and a full field turning away what does not
// fit; and an editor over a text editor, through its modes, keys, the
// selection, the clipboard, undo and redo.
import std;
import vec;
import math;
import ui;

let FAR: i64 = 900;

// The mouse at x, y with the left button as given; keys and text follow.
fn frame_input(ctx: ui::Context, x: i64, y: i64, down: bool) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x, y);
    ui::input_button(ctx, ui::BUTTON_LEFT, x, y, down);
}

// A key pressed and released within a frame, as a typed key comes. Nuklear
// looks at a frame's keys after it, in the order of their KEY_* values.
fn tap(ctx: ui::Context, key: i64) {
    ui::input_key(ctx, key, true);
    ui::input_key(ctx, key, false);
}

// Text with its line breaks and its bytes past ASCII escaped.
fn escaped(text: const u8[:]) -> u8[>..] {
    let digits = "0123456789abcdef";
    var out: u8[>..] = [];
    for ch in text {
        if ch == '\n' {
            out.append("\\n");
        } else if ch >= 128 {
            out.append("\\x");
            out.push(digits[ch >> 4]);
            out.push(digits[ch & 15]);
        } else {
            out.push(ch);
        }
    }
    out
}

fn show(e: ui::TextEdit) -> u8[>..] {
    let s = ui::state(e);
    str("'", escaped(ui::text(e)), "' cursor ", s.cursor, " selection ", s.select_start, "..",
        s.select_end, " length ", s.length)
}

fn events(r: i64) -> u8[>..] {
    var out: u8[>..] = [];
    if (r & ui::EDIT_ACTIVE) != 0 { out.append(" active"); }
    if (r & ui::EDIT_INACTIVE) != 0 { out.append(" inactive"); }
    if (r & ui::EDIT_ACTIVATED) != 0 { out.append(" activated"); }
    if (r & ui::EDIT_DEACTIVATED) != 0 { out.append(" deactivated"); }
    if (r & ui::EDIT_COMMITTED) != 0 { out.append(" committed"); }
    out
}

fn main() {
    // --- filters ---
    let names: const u8[:][7] = ["default", "ascii", "float", "decimal", "hex", "oct", "binary"];
    let probes: u32[8] = ['a', 'F', '7', '9', '1', '.', '-', 0xE9];
    for f in 7 {
        var accepted: u8[>..] = [];
        for p in probes { accepted.push(if ui::filter_accepts(f, p) { '+' } else { '.' }); }
        print("filter ", names[f], ": ", accepted, " (a F 7 9 1 . - e-acute)");
    }

    // --- a text editor through its own operations ---
    let e = ui::create_text_edit();
    print("editor valid ", ui::is_valid(e), ": ", show(e), ", mode ", ui::state(e).mode,
          ", single line ", ui::state(e).single_line);
    ui::textedit_text(e, "ignored");
    print("typing in view mode: ", show(e));
    ui::set_mode(e, ui::TEXT_EDIT_MODE_INSERT);
    ui::textedit_text(e, "hello world");
    print("typed: ", show(e));
    ui::set_cursor(e, 5, 5, 5);
    ui::textedit_text(e, ",");
    print("comma at 5: ", show(e));
    ui::textedit_delete(e, 0, 1);
    print("first deleted: ", show(e));
    ui::set_cursor(e, 0, 0, 4);
    print("cut 'ello': ", ui::textedit_cut(e), " ", show(e));
    print("paste 'HELLO': ", ui::textedit_paste(e, "HELLO"), " ", show(e));
    ui::textedit_undo(e);
    print("undone: ", show(e));
    ui::textedit_redo(e);
    print("redone: ", show(e));
    ui::textedit_select_all(e);
    print("all selected: ", show(e));
    ui::textedit_delete_selection(e);
    print("deleted: ", show(e));
    ui::textedit_undo(e);
    print("undone: ", show(e));
    ui::set_mode(e, ui::TEXT_EDIT_MODE_REPLACE);
    ui::set_cursor(e, 0, 0, 0);
    ui::textedit_text(e, "J");
    print("replace mode: ", show(e));
    ui::set_text(e, "fresh");
    print("set_text: ", show(e));
    ui::textedit_undo(e);
    print("nothing to undo after set_text: ", show(e));
    ui::set_cursor(e, 5, 5, 5);
    print("paste of longer characters: ", ui::textedit_paste(e, "-\xc3\xa9\xe2\x82\xac"), " ", show(e));
    ui::set_mode(e, ui::TEXT_EDIT_MODE_VIEW);
    print("paste in view mode: ", ui::textedit_paste(e, "no"), " ", show(e));

    let ctx = ui::create(13.0);

    // --- a field over a byte buffer: clicked into, typed into, left ---
    var buffer: u8[32] = [0; 32];
    var len = 0;
    var field = ui::rect(0, 0, 0, 0);
    for step in 10 {
        let at = if step >= 1 && step <= 2 { field } else { ui::rect(FAR, FAR, 0, 0) };
        frame_input(ctx, (at.x + 20.0) as! i64, (at.y + at.h / 2.0) as! i64,
                    step == 1 || step == 9);
        if step == 3 { ui::input_char(ctx, 'G'); }
        if step == 4 { ui::input_unicode(ctx, 'o'); }
        if step == 5 {
            let o: u8[1] = ['o'];
            ui::input_glyph(ctx, o);
        }
        if step == 6 { ui::input_text(ctx, "se!"); }
        if step == 7 { tap(ctx, ui::KEY_BACKSPACE); }
        if step == 8 { tap(ctx, ui::KEY_ENTER); }
        ui::input_end(ctx);
        if ui::begin(ctx, "Edit", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            field = ui::widget_bounds(ctx);
            let r = ui::edit_string(ctx, ui::EDIT_FIELD | ui::EDIT_SIG_ENTER, buffer, len,
                                    ui::FILTER_DEFAULT);
            print("step ", step, ": '", buffer[..len], "'", events(r));
        }
        ui::end(ctx);
    }

    // --- flags, seen through a click at a field's start and a typed Z ---
    let flag_names: const u8[:][5] = ["field", "auto select", "go to end", "no cursor",
                                       "read only"];
    let flag_sets: i64[5] = [ui::EDIT_FIELD, ui::EDIT_FIELD | ui::EDIT_AUTO_SELECT,
                             ui::EDIT_FIELD | ui::EDIT_GOTO_END_ON_ACTIVATE,
                             ui::EDIT_FIELD | ui::EDIT_NO_CURSOR,
                             ui::EDIT_FIELD | ui::EDIT_READ_ONLY];
    for k in 5 {
        var text: u8[..8] = [];
        text.append("abc");
        var at = ui::rect(0, 0, 0, 0);
        for step in 3 {
            let x = if step == 0 { FAR } else { (at.x + 2.0) as! i64 };
            frame_input(ctx, x, (at.y + at.h / 2.0) as! i64, step == 1);
            if step == 2 { ui::input_text(ctx, "Z"); }
            ui::input_end(ctx);
            if ui::begin(ctx, str("Flags ", k), ui::rect(0, 0, 300, 300), 0) {
                ui::layout_row_dynamic(ctx, 25.0, 1);
                at = ui::widget_bounds(ctx);
                ui::edit_string(ctx, flag_sets[k], text, ui::FILTER_DEFAULT);
            }
            ui::end(ctx);
        }
        print("flags ", flag_names[k], ": '", text, "'");
    }

    // --- a field over a limited array, focused by the program ---
    var name: u8[..8] = [];
    name.append("abc");
    for step in 6 {
        frame_input(ctx, FAR, FAR, false);
        if step == 1 { ui::input_text(ctx, "defgh"); }
        if step == 2 { ui::input_text(ctx, "overflow"); }
        if step == 3 { tap(ctx, ui::KEY_DEL); }
        if step == 4 {
            tap(ctx, ui::KEY_TEXT_START);
            ui::input_text(ctx, "X");
        }
        if step == 5 { ui::input_text(ctx, "unseen"); }
        ui::input_end(ctx);
        if ui::begin(ctx, "Name", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            if step == 0 { ui::edit_focus(ctx, ui::EDIT_ALWAYS_INSERT_MODE); }
            if step == 5 { ui::edit_unfocus(ctx); }
            let r = ui::edit_string(ctx, ui::EDIT_FIELD, name, ui::FILTER_DEFAULT);
            print("name step ", step, ": '", name, "' (", name.len, " of ", name.cap, ")",
                  events(r));
        }
        ui::end(ctx);
    }

    // --- a full field: typing and pasting take only what fits ---
    var full: u8[4] = [0; 4];
    var full_len = 0;
    for step in 13 {
        if step == 2 { ui::set_clipboard(ctx, "wxyz"); }
        if step == 12 { ui::set_clipboard(ctx, "\xc3\xa9\xe2\x82\xac"); }
        frame_input(ctx, FAR, FAR, false);
        if step == 1 { ui::input_text(ctx, "ab"); }
        if step == 2 || step == 12 { tap(ctx, ui::KEY_PASTE); }
        if step == 3 || step == 5 { ui::input_text(ctx, "!"); }
        if step == 4 { tap(ctx, ui::KEY_LEFT); }
        if step == 6 { tap(ctx, ui::KEY_DEL); }
        if step == 7 { tap(ctx, ui::KEY_TEXT_REPLACE_MODE); }
        if step == 8 { ui::input_text(ctx, "\xc3\xa9Z"); }
        if step == 9 { tap(ctx, ui::KEY_TEXT_START); }
        if step == 10 { ui::input_text(ctx, "\xc3\xa9Y"); }
        if step == 11 { tap(ctx, ui::KEY_TEXT_SELECT_ALL); }
        ui::input_end(ctx);
        if ui::begin(ctx, "Full", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            if step == 0 { ui::edit_focus(ctx, ui::EDIT_ALWAYS_INSERT_MODE); }
            ui::edit_string(ctx, ui::EDIT_SELECTABLE | ui::EDIT_CLIPBOARD, full, full_len,
                            ui::FILTER_DEFAULT);
            print("full step ", step, ": '", escaped(full[..full_len]), "'");
        }
        ui::end(ctx);
    }

    // --- an editor over a text editor: modes, keys, the clipboard ---
    let doc = ui::create_text_edit();
    ui::set_text(doc, "one two three");
    ui::set_clipboard(ctx, "PASTED");
    var box = ui::rect(0, 0, 0, 0);
    for step in 25 {
        let at = if step >= 1 && step <= 2 { box } else { ui::rect(FAR, FAR, 0, 0) };
        frame_input(ctx, (at.x + 10.0) as! i64, (at.y + 10.0) as! i64, step == 1 || step == 24);
        if step == 2 { ui::input_text(ctx, "not typed"); }
        if step == 3 || step == 13 { tap(ctx, ui::KEY_TEXT_INSERT_MODE); }
        if step == 4 || step == 15 { tap(ctx, ui::KEY_TEXT_END); }
        if step == 5 {
            // Shift held down at the frame's end: the cursor's move selects.
            ui::input_key(ctx, ui::KEY_SHIFT, true);
            tap(ctx, ui::KEY_TEXT_WORD_LEFT);
        }
        if step == 6 {
            ui::input_key(ctx, ui::KEY_SHIFT, false);
            tap(ctx, ui::KEY_COPY);
        }
        if step == 7 { tap(ctx, ui::KEY_CUT); }
        if step == 8 { tap(ctx, ui::KEY_PASTE); }
        if step == 9 { tap(ctx, ui::KEY_TEXT_UNDO); }
        if step == 10 { tap(ctx, ui::KEY_TEXT_REDO); }
        if step == 11 {
            tap(ctx, ui::KEY_TEXT_LINE_START);
            tap(ctx, ui::KEY_TEXT_WORD_RIGHT);
            tap(ctx, ui::KEY_TEXT_REPLACE_MODE);
        }
        if step == 12 { ui::input_text(ctx, "TWO"); }
        if step == 14 { tap(ctx, ui::KEY_TAB); }
        if step == 16 { tap(ctx, ui::KEY_ENTER); }
        if step == 17 { ui::input_text(ctx, "four"); }
        // At the text's very end the cursor is on a row of its own, below
        // the last: up from within the row.
        if step == 18 { tap(ctx, ui::KEY_LEFT); }
        if step == 19 { tap(ctx, ui::KEY_UP); }
        if step == 20 { tap(ctx, ui::KEY_DOWN); }
        if step == 21 { tap(ctx, ui::KEY_TEXT_RESET_MODE); }
        if step == 22 {
            ui::input_text(ctx, "not typed");
            tap(ctx, ui::KEY_DEL);
        }
        if step == 23 { tap(ctx, ui::KEY_TEXT_SELECT_ALL); }
        ui::input_end(ctx);
        if step == 5 || step == 6 {
            print("keys: shift down ", ui::input_is_key_down(ctx, ui::KEY_SHIFT), ", released ",
                  ui::input_is_key_released(ctx, ui::KEY_SHIFT), "; copy pressed ",
                  ui::input_is_key_pressed(ctx, ui::KEY_COPY));
        }
        if ui::begin(ctx, "Doc", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 60.0, 1);
            box = ui::widget_bounds(ctx);
            let r = ui::edit_buffer(ctx, ui::EDIT_EDITOR, doc, ui::FILTER_DEFAULT);
            let copied, any = ui::copied(ctx);
            print("doc step ", step, ": ", show(doc), ", mode ", ui::state(doc).mode, events(r),
                  if any { str(", copied '", copied, "'") } else { str("") });
        }
        ui::end(ctx);
    }

    // A field taking hexadecimal digits only.
    var hex: u8[..6] = [];
    for step in 2 {
        frame_input(ctx, FAR, FAR, false);
        if step == 1 { ui::input_text(ctx, "c0ffee!xyz"); }
        ui::input_end(ctx);
        if ui::begin(ctx, "Hex", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            if step == 0 { ui::edit_focus(ctx, 0); }
            ui::edit_string(ctx, ui::EDIT_SIMPLE, hex, ui::FILTER_HEX);
        }
        ui::end(ctx);
    }
    print("hex field: '", hex, "'");

    frame_input(ctx, FAR, FAR, false);
    ui::input_end(ctx);
    ui::destroy(ctx);
    ui::destroy(doc);
    ui::destroy(e);
    print("editors valid ", ui::is_valid(doc), " ", ui::is_valid(e), "; misuses ",
          ui::misuse_count());
}
