// Backend containment: full resizable payload views remain unsupported.
enum Value { A { items: i64[>..] } }
fn size(a: Value.A&) -> i64 { a.items.len }
fn main() { var v: Value.. = Value.A { items: [1, 2] }; print(size(v)); }
