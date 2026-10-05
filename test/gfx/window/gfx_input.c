/* The real-window half of gfx_mouse_capture.goose. A hidden SDL window
   exercises the platform setters without taking desktop focus or moving
   the user's pointer. Run with run_input_test.py. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include "../../../src/gfx/gfx_api.h"

#include <stdio.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (%s)\n", what, SDL_GetError());
        failures++;
    }
}

int main(void) {
    /* Keep SDL alive after gfx closes, so cursor cleanup can be inspected. */
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    gs_gfx_bytes title = { (uint8_t *)"Goose input regression", 22 };
    if (!gs_gfx_open(title, 64, 48, GS_GFX_WINDOW_HIDDEN)) {
        uint8_t why[1024] = { 0 };
        gs_gfx_bytes out = { why, sizeof why - 1 };
        gs_gfx_error(out);
        fprintf(stderr, "gfx open: %s\n", why);
        SDL_Quit();
        return 1;
    }
    int n = 0;
    SDL_Window **windows = SDL_GetWindows(&n);
    if (n != 1) {
        fprintf(stderr, "expected one actual SDL window, found %d\n", n);
        SDL_free(windows);
        gs_gfx_close();
        SDL_Quit();
        return 1;
    }
    SDL_Window *window = windows[0];
    SDL_free(windows);
    SDL_WindowID id = SDL_GetWindowID(window);
    gs_gfx_frame();
    check(!gs_gfx_focused(), "a hidden window starts without focus");
    check(!gs_gfx_set_mouse_relative(true), "capture without focus is rejected");

    /* Injected focus changes gfx's state, never actual desktop focus. */
    gs_gfx_inject_focus(true);
    gs_gfx_frame();
    check(gs_gfx_set_cursor_visible(false), "hide the cursor");
    check(!SDL_CursorVisible(), "hiding reaches SDL");
    check(gs_gfx_set_mouse_relative(true), "enable relative mode");
    check(SDL_GetWindowRelativeMouseMode(window), "relative mode reaches the SDL window");
    check(gs_gfx_set_cursor_visible(true), "remember visible preference in relative mode");
    check(!SDL_CursorVisible(), "relative mode still hides the SDL cursor");
    check(gs_gfx_set_mouse_relative(false), "disable relative mode");
    check(!SDL_GetWindowRelativeMouseMode(window), "relative mode disabled in SDL");
    check(SDL_CursorVisible(), "visible preference restored in SDL");
    check(gs_gfx_set_mouse_relative(true), "capture again");

    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = id + 1;
    SDL_PushEvent(&event);
    gs_gfx_frame();
    check(gs_gfx_focused() && gs_gfx_mouse_relative(), "another window's focus is ignored");

    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 2;
    SDL_PushEvent(&event);
    gs_gfx_frame();
    gs_gfx_inject_focus(false);
    gs_gfx_frame();
    check(!gs_gfx_focused(), "focus loss reaches the state query");
    check(!SDL_GetWindowRelativeMouseMode(window), "focus loss releases SDL relative mode");
    check(SDL_CursorVisible(), "focus loss shows the SDL cursor");
    check(gs_gfx_mouse_released(GS_GFX_MOUSE_LEFT), "focus loss releases a held button");
    gs_gfx_event events[8];
    gs_gfx_event_slice out = { events, 8 };
    int64_t count = gs_gfx_events(out);
    check(count == 2 && events[0].kind == GS_GFX_EVENT_MOUSE_BUTTON &&
          !events[0].down && events[0].clicks == 2 &&
          events[1].kind == GS_GFX_EVENT_FOCUS_LOST,
          "double-click release precedes the focus notification");

    gs_gfx_inject_focus(true);
    gs_gfx_frame();
    check(!SDL_GetWindowRelativeMouseMode(window), "focus gain does not recapture");
    check(gs_gfx_set_mouse_relative(true), "explicitly recapture");
    gs_gfx_close();
    check(SDL_CursorVisible(), "close restores cursor visibility");
    check(gs_gfx_misuse_count() == 0, "no gfx misuse");
    SDL_Quit();
    if (!failures) puts("gfx window input passed");
    return failures ? 1 : 0;
}
