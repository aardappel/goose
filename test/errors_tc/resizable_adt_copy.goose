// error: copying a resizable ADT or variant is not supported by the C backend yet; construct a fresh value or pass the owning value by reference
// Backend containment: full resizable payload views remain unsupported.
enum Value { A { items: i64[>..] } }
fn main() { var v: Value.. = Value.A { items: [1, 2] }; var c = copy(v); }
