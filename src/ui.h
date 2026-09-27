// The compiler's side of the optional ui module (stdlib/ui.goose): whether
// the layer over Nuklear (src/ui/) is built in, which a JIT run of a program
// using it needs, and where a program built from the generated C finds it
// to link. Without it, such a program still typechecks and emits the same C.

#pragma once

namespace goose {

#ifdef GOOSE_HAVE_UI
inline constexpr bool have_ui = true;
#else
inline constexpr bool have_ui = false;
#endif

// What running a ui program in this process says when the layer is not
// built in. The test runners report it as a skip.
inline const char *no_ui_error = "this compiler was built without Nuklear; check out "
                                 "third_party/nuklear and reconfigure";

// The response file of link inputs a program built from the generated C
// needs to use ui (cmake/ui.cmake), for --ui-link.
inline string UiLinkFile(const string &exedir, const string &style) {
    #ifdef GOOSE_UI_LINK_PATH
        const char *built = GOOSE_UI_LINK_PATH;
    #else
        const char *built = nullptr;
    #endif
    return NativeLinkFile("--ui-link", have_ui, no_ui_error, "ui", "GOOSE_UI_LINK", built, exedir,
                          style);
}

}  // namespace goose
