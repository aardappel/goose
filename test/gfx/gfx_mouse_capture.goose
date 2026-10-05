// First-person input headless: mode/visibility changes, stale motion,
// ordered focus transitions, ignored background input, and close/reopen.
import std;
import vec;
import os;
import gfx;

fn open_or_skip() -> bool {
    if gfx::open_headless(32, 24) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn state(label: const u8[:]) {
    print(label, ": focused ", gfx::focused(), ", relative ", gfx::mouse_relative(),
          ", visible ", gfx::cursor_visible());
}

fn main() {
    guard open_or_skip() else { return; }
    gfx::frame();
    state("initial");

    print("hide ", gfx::set_cursor_visible(false));
    state("hidden");
    print("capture ", gfx::set_mouse_relative(true));
    print("prefer visible ", gfx::set_cursor_visible(true));
    state("captured");
    print("release ", gfx::set_mouse_relative(false));
    state("restored");

    // A mode change drops motion queued before it, even before frame().
    gfx::inject_mouse(8.0, 6.0, 0, false);
    gfx::set_mouse_relative(true);
    gfx::frame();
    print("pending motion discarded ", gfx::mouse_delta() == float2_0);
    gfx::inject_mouse(100.0, -30.0, 0, false);
    gfx::frame();
    print("unbounded delta ", gfx::mouse_delta());
    gfx::set_mouse_relative(true);
    print("same mode preserves delta ", gfx::mouse_delta());
    gfx::set_mouse_relative(false);
    print("changed mode clears delta ", gfx::mouse_delta() == float2_0);

    // A hidden-cursor preference survives manually toggling relative mode.
    gfx::set_cursor_visible(false);
    gfx::set_mouse_relative(true);
    gfx::set_mouse_relative(false);
    state("hidden preference");

    gfx::set_mouse_relative(true);
    gfx::inject_key("W", true);
    gfx::inject_mouse(5.0, 6.0, gfx::MOUSE_LEFT, true);
    gfx::frame();
    print("held before loss ", gfx::key_down("W"), ", ", gfx::mouse_down(gfx::MOUSE_LEFT));
    print("queue loss ", gfx::inject_focus(false));
    print("still focused before frame ", gfx::focused());
    gfx::frame();
    state("lost focus");
    print("held cleared ", !gfx::key_down("W"), ", ", !gfx::mouse_down(gfx::MOUSE_LEFT));
    print("release edges ", gfx::key_released("W"), ", ", gfx::mouse_released(gfx::MOUSE_LEFT));
    let loss = gfx::events();
    print("releases before focus loss ", loss.len == 3 &&
          loss[0].kind == gfx::EVENT_KEY && !loss[0].down &&
          loss[1].kind == gfx::EVENT_MOUSE_BUTTON && !loss[1].down &&
          loss[2].kind == gfx::EVENT_FOCUS_LOST);
    print("focus motion cleared ", gfx::mouse_delta() == float2_0);
    print("background capture rejected ", !gfx::set_mouse_relative(true),
          ", reason ", gfx::error().len > 0);
    print("background hide rejected ", !gfx::set_cursor_visible(false));

    // Loss duplicates do not emit more events. None of this background input
    // leaks into the frame that regains focus; input afterwards does.
    gfx::inject_focus(false);
    gfx::inject_key("A", true);
    gfx::inject_mouse(8.0, 9.0, gfx::MOUSE_RIGHT, true);
    gfx::inject_text("ignored");
    gfx::inject_focus(true);
    gfx::inject_focus(true);
    gfx::inject_text("!");
    gfx::frame();
    state("regained focus");
    print("background input ignored ", !gfx::key_down("A"), ", ",
          !gfx::mouse_down(gfx::MOUSE_RIGHT));
    let events = gfx::events();
    print("regain then text ", events.len == 2 &&
          events[0].kind == gfx::EVENT_FOCUS_GAINED && events[1].kind == gfx::EVENT_TEXT);

    // Losing and regaining in a single frame is visible in the event stream,
    // even though the final focused() state is the same.
    gfx::set_mouse_relative(true);
    gfx::inject_focus(false);
    gfx::inject_focus(true);
    gfx::frame();
    let changes = gfx::events();
    print("both focus edges ", changes.len == 2 &&
          changes[0].kind == gfx::EVENT_FOCUS_LOST &&
          changes[1].kind == gfx::EVENT_FOCUS_GAINED);
    state("brief loss");
    print("explicit resume ", gfx::set_mouse_relative(true));
    state("resumed");

    gfx::inject_key("S", true);
    gfx::inject_mouse(5.0, 6.0, gfx::MOUSE_LEFT, true);
    gfx::inject_focus(false);
    gfx::frame();
    print("press then focus loss retains both edges ",
          gfx::key_pressed("S") && gfx::key_released("S") &&
          gfx::mouse_pressed(gfx::MOUSE_LEFT) && gfx::mouse_released(gfx::MOUSE_LEFT));
    gfx::inject_focus(true);
    gfx::frame();
    print("release edges clear next frame ",
          !gfx::key_released("S") && !gfx::mouse_released(gfx::MOUSE_LEFT));
    gfx::set_mouse_relative(true);

    gfx::close();
    guard open_or_skip() else { return; }
    state("reopened");
    gfx::close();
}
