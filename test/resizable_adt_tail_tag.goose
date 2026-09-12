// A struct with a resizable ADT tail is not a frame object (C.2): its header's
// base is the struct's start, so tag matching reads the tag at an offset from it.
enum Value { A { items: i64[>..] }, B { n: i64 } }
struct Tagged { id: i64, v: Value.. }
fn byref(t: Tagged&) {
    match t.v { A => print("ref tA ", t.id), B b => print("ref tB ", b.n, " ", t.id) }
}
fn byval(t: Tagged) {
    match t.v { A => print("val tA ", t.id), B b => print("val tB ", b.n, " ", t.id) }
}
fn main() {
    var t = Tagged { id: 3, v: Value.B { n: 4 } };
    match t.v { A => print("local tA ", t.id), B b => print("local tB ", b.n, " ", t.id) }
    byref(t);
    byval(Tagged { id: 3, v: Value.B { n: 4 } });
    var u = Tagged { id: 7, v: Value.A { items: [8] } };
    match u.v { A => print("local tA ", u.id), B b => print("local tB ", b.n, " ", u.id) }
    byref(u);
    byval(Tagged { id: 7, v: Value.A { items: [8] } });
}
