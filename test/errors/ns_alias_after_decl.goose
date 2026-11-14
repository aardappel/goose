// error: a namespace alias must precede all declarations
// An alias comes before the declarations, so it is in place before any
// reference it rewrites.
struct geo::Pt { x: i64 }
namespace g = geo;
fn main() {}
