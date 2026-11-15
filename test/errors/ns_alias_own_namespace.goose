// error: namespace alias geo is this file's own namespace
// A file's own namespace needs no alias, and taking its name for one would
// hide it.
namespace geo;
namespace geo = shapes;
struct shapes::Sq { s: i64 }
fn ::main() {}
