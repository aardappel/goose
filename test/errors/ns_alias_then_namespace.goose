// error: namespace g is an alias in this file
// A file's namespace may not take the name of one of its aliases.
namespace g = geo;
namespace g;
struct geo::Pt { x: i64 }
fn ::main() {}
