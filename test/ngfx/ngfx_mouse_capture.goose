// First-person input headless: mode/visibility changes, stale motion,
// ordered focus transitions, ignored background input, and close/reopen.
import std;
import vec;
import os;
import ngfx;

fn open_or_skip() -> bool {
    if ngfx::open_headless(32, 24) { return true; }
    write_stderr(str("ngfx: ", ngfx::error(), "\n"));
    return false;
}

fn state(label: const u8[:]) {
    print(label, ": focused ", ngfx::focused(), ", relative ", ngfx::mouse_relative(),
          ", visible ", ngfx::cursor_visible());
}

fn main() {
    guard open_or_skip() else { return; }
    ngfx::frame();
    state("initial");

    print("hide ", ngfx::set_cursor_visible(false));
    state("hidden");
    print("capture ", ngfx::set_mouse_relative(true));
    print("prefer visible ", ngfx::set_cursor_visible(true));
    state("captured");
    print("release ", ngfx::set_mouse_relative(false));
    state("restored");

    // A mode change drops motion queued before it, even before frame().
    ngfx::inject_mouse(8.0, 6.0, 0, false);
    ngfx::set_mouse_relative(true);
    ngfx::frame();
    print("pending motion discarded ", ngfx::mouse_delta() == float2_0);
    ngfx::inject_mouse(100.0, -30.0, 0, false);
    ngfx::frame();
    print("unbounded delta ", ngfx::mouse_delta());
    ngfx::set_mouse_relative(true);
    print("same mode preserves delta ", ngfx::mouse_delta());
    ngfx::set_mouse_relative(false);
    print("changed mode clears delta ", ngfx::mouse_delta() == float2_0);

    // A hidden-cursor preference survives manually toggling relative mode.
    ngfx::set_cursor_visible(false);
    ngfx::set_mouse_relative(true);
    ngfx::set_mouse_relative(false);
    state("hidden preference");

    ngfx::set_mouse_relative(true);
    ngfx::inject_key("W", true);
    ngfx::inject_mouse(5.0, 6.0, ngfx::MOUSE_LEFT, true);
    ngfx::frame();
    print("held before loss ", ngfx::key_down("W"), ", ", ngfx::mouse_down(ngfx::MOUSE_LEFT));
    print("queue loss ", ngfx::inject_focus(false));
    print("still focused before frame ", ngfx::focused());
    ngfx::frame();
    state("lost focus");
    print("held cleared ", !ngfx::key_down("W"), ", ", !ngfx::mouse_down(ngfx::MOUSE_LEFT));
    print("release edges ", ngfx::key_released("W"), ", ", ngfx::mouse_released(ngfx::MOUSE_LEFT));
    let loss = ngfx::events();
    print("releases before focus loss ", loss.len == 3 &&
          loss[0].kind == ngfx::EVENT_KEY && !loss[0].down &&
          loss[1].kind == ngfx::EVENT_MOUSE_BUTTON && !loss[1].down &&
          loss[2].kind == ngfx::EVENT_FOCUS_LOST);
    print("focus motion cleared ", ngfx::mouse_delta() == float2_0);
    print("background capture rejected ", !ngfx::set_mouse_relative(true),
          ", reason ", ngfx::error().len > 0);
    print("background hide rejected ", !ngfx::set_cursor_visible(false));

    // Loss duplicates do not emit more events. None of this background input
    // leaks into the frame that regains focus; input afterwards does.
    ngfx::inject_focus(false);
    ngfx::inject_key("A", true);
    ngfx::inject_mouse(8.0, 9.0, ngfx::MOUSE_RIGHT, true);
    ngfx::inject_text("ignored");
    ngfx::inject_focus(true);
    ngfx::inject_focus(true);
    ngfx::inject_text("!");
    ngfx::frame();
    state("regained focus");
    print("background input ignored ", !ngfx::key_down("A"), ", ",
          !ngfx::mouse_down(ngfx::MOUSE_RIGHT));
    let events = ngfx::events();
    print("regain then text ", events.len == 2 &&
          events[0].kind == ngfx::EVENT_FOCUS_GAINED && events[1].kind == ngfx::EVENT_TEXT);

    // Losing and regaining in a single frame is visible in the event stream,
    // even though the final focused() state is the same.
    ngfx::set_mouse_relative(true);
    ngfx::inject_focus(false);
    ngfx::inject_focus(true);
    ngfx::frame();
    let changes = ngfx::events();
    print("both focus edges ", changes.len == 2 &&
          changes[0].kind == ngfx::EVENT_FOCUS_LOST &&
          changes[1].kind == ngfx::EVENT_FOCUS_GAINED);
    state("brief loss");
    print("explicit resume ", ngfx::set_mouse_relative(true));
    state("resumed");

    ngfx::inject_key("S", true);
    ngfx::inject_mouse(5.0, 6.0, ngfx::MOUSE_LEFT, true);
    ngfx::inject_focus(false);
    ngfx::frame();
    print("press then focus loss retains both edges ",
          ngfx::key_pressed("S") && ngfx::key_released("S") &&
          ngfx::mouse_pressed(ngfx::MOUSE_LEFT) && ngfx::mouse_released(ngfx::MOUSE_LEFT));
    ngfx::inject_focus(true);
    ngfx::frame();
    print("release edges clear next frame ",
          !ngfx::key_released("S") && !ngfx::mouse_released(ngfx::MOUSE_LEFT));
    ngfx::set_mouse_relative(true);

    ngfx::close();
    guard open_or_skip() else { return; }
    state("reopened");
    ngfx::close();
}
