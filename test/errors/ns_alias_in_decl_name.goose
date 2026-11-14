// error: a declaration names its namespace, not an alias: g is an alias of geo
// A declaration names the namespace it goes in by that namespace's name.
namespace g = geo;
struct geo::Pt { x: i64 }
fn g::norm(p: geo::Pt) -> i64 { p.x }
fn main() {}
