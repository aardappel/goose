// error: unknown namespace gep in namespace alias g
// An alias's target must be a namespace some file declares.
namespace g = gep;
struct geo::Pt { x: i64 }
fn main() {}
