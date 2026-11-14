// error: namespace alias g declared twice in this file
// A file declares each alias once.
namespace g = geo;
namespace g = geo;
struct geo::Pt { x: i64 }
fn main() {}
