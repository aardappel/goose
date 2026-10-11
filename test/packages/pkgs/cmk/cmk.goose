// A package built with CMake: its sidecar names the static library target
// of the CMakeLists.txt at its root (docs/design/packages.md §5.2). The
// compiler configures and builds a wrapper project around it, takes the
// target's whole link line for goose build, and builds the shared library
// goose run loads from the same objects.
namespace cmk;

extern fn cmk_triple(x: i32) -> i32;

fn triple(x: i32) -> i32 { cmk_triple(x) }
