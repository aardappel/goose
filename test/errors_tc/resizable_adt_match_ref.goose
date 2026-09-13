// error: binding a resizable ADT payload is not supported by the C backend yet; match its tag without a payload binder, or use a standalone resizable struct
// Backend containment: full resizable payload views remain unsupported.
enum Value { A { items: i64[>..] } }
fn main() { var v: Value.. = Value.A { items: [1, 2] }; match v { A &a => print(a.items.len) } }
