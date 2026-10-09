// The compiler's side of the optional ngfx module (stdlib/ngfx.goose):
// whether the layer over NoGraphicsAPI (src/ngfx/) is built in, which a JIT
// run of a program using it needs, and where a program built from the
// generated C finds it to link. Without it, such a program still typechecks
// and emits the same C. The shaders it embeds are compiled by src/slangc.h.

#pragma once

namespace goose {

#ifdef GOOSE_HAVE_NGFX
inline constexpr bool have_ngfx = true;
#else
inline constexpr bool have_ngfx = false;
#endif

// What running an ngfx program in this process says when the layer is not
// built in. The test runners report it as a skip.
inline const char *no_ngfx_error = "this compiler was built without ngfx, which needs "
                                   "third_party/NoGraphicsAPI, third_party/SDL, slangc and a "
                                   "Metal 4 or Vulkan 1.4 toolchain (cmake/ngfx.cmake)";

// The response file of link inputs a program built from the generated C
// needs to use ngfx (cmake/ngfx.cmake), for --ngfx-link.
inline string NgfxLinkFile(const string &exedir, const string &style) {
    #ifdef GOOSE_NGFX_LINK_PATH
        const char *built = GOOSE_NGFX_LINK_PATH;
    #else
        const char *built = nullptr;
    #endif
    return NativeLinkFile("--ngfx-link", have_ngfx, no_ngfx_error, "ngfx", "GOOSE_NGFX_LINK", built,
                          exedir, style);
}

}  // namespace goose
