// Backend containment: full resizable payload views remain unsupported.
enum Value { A { items: i64[>..] } }
fn main() { var v: Value.. = Value.A { items: [1, 2] }; var c = copy(v); }
