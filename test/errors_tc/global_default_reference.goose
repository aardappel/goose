// error: global current needs an initializer, since Node& has no default value: Node& is a non-optional reference
// Whatever the type, a var global declared without an initializer starts as
// its default value (§11.1), `[]` for the array here; a non-optional
// reference has none, having nothing to point at.
struct Node { v: i64 }

var nodes: Node[>..];
var current: Node&;

fn main() { print(nodes.len); }
