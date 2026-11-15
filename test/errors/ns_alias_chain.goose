// error: a namespace alias names a namespace, not an alias: g is an alias of geo
// An alias's target is a namespace, never another alias.
namespace g = geo;
namespace h = g;
struct geo::Pt { x: i64 }
fn main() {}
