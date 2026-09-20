// The compiler's side of the optional physics module (stdlib/physics.goose):
// whether the layer over Box3D (src/physics/) is built in, which a JIT run of
// a program using it needs, and where a program built from the generated C
// finds it to link. Without it, such a program still typechecks and emits
// the same C.

#pragma once

namespace goose {

#ifdef GOOSE_HAVE_PHYSICS
inline constexpr bool have_physics = true;
#else
inline constexpr bool have_physics = false;
#endif

// What running a physics program in this process says when the layer is
// not built in. The test runners report it as a skip.
inline const char *no_physics_error = "this compiler was built without Box3D; check out "
                                      "third_party/box3d and reconfigure";

// The response file of link inputs a program built from the generated C
// needs to use physics (cmake/physics.cmake), for --physics-link.
inline string PhysicsLinkFile(const string &exedir, const string &style) {
    #ifdef GOOSE_PHYSICS_LINK_PATH
        const char *built = GOOSE_PHYSICS_LINK_PATH;
    #else
        const char *built = nullptr;
    #endif
    return NativeLinkFile("--physics-link", have_physics, no_physics_error, "physics",
                          "GOOSE_PHYSICS_LINK", built, exedir, style);
}

}  // namespace goose
