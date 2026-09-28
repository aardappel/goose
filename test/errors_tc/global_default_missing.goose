// error: global link needs an initializer, since Link has no default value: field to has no declared default and Node& is a non-optional reference
// A var global declared without an initializer starts as its type's
// default value (§11.1), which a type holding a non-optional reference does
// not have.
struct Node { v: i64 }
struct Link { weight: i64, to: Node& }

var link: Link;

fn main() { print(link.weight); }
