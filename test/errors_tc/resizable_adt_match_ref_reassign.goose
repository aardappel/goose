// A whole assignment of a resizable ADT replaces the variant under a payload reference.
enum Value { A { items: i64[>..] }, B { n: i64 }, C { s: u8[] } }
fn main() {
    var v: Value.. = Value.B { n: 7 };
    match v { B &b => { v = Value.C { s: "overwrite" }; print(b.n); }, A => print("A"), C => print("C") }
}
