// A package with a native part: its sidecar names the C under src/ and the
// headers under include/ (docs/design/packages.md §5.1). A program that
// calls these extern fns links the package's objects, or loads its library
// under the JIT.
namespace cmath2;

extern fn cmath2_square(x: i32) -> i32;
extern fn cmath2_dot(a: const i32[:], b: const i32[:]) -> i64;

fn square(x: i32) -> i32 { cmath2_square(x) }
fn dot(a: const i32[:], b: const i32[:]) -> i64 { cmath2_dot(a, b) }
