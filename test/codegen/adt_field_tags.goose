// Tag matching on a variable-mode ADT field reads the tag at the field's offset
// from its owner: after fixed fields, past a variable-size field, or in a payload.
enum V { A { s: u8[] }, B { n: i64 } }
struct S { id: i64, v: V.. }
struct P { name: u8[], id: i32, v: V.. }
enum W { Wrap { id: i64, v: V.. }, Other }
fn byref(s: S&) { match s.v { A a => print("ref A ", a.s, " ", s.id), B b => print("ref B ", b.n, " ", s.id) } }
fn variable_field() {
    var s = S { id: 3, v: V.B { n: 4 } };
    match s.v { A => print("local A ", s.id), B b => print("local B ", b.n, " ", s.id) }
    byref(s);
    var u = S { id: 7, v: V.A { s: "hi" } };
    match u.v { A a => print("local A ", a.s, " ", u.id), B b => print("local B ", b.n, " ", u.id) }
    byref(u);
    var p = P { name: "nm", id: 6, v: V.A { s: "hello" } };
    match p.v { A a => print("prefix A ", a.s, " ", p.id), B b => print("prefix B ", b.n, " ", p.id) }
    var w: W.. = W.Wrap { id: 9, v: V.B { n: 10 } };
    match w { Wrap &x => match x.v { A a => print("payload A ", a.s), B b => print("payload B ", b.n, " ", x.id) }, Other => print("other") }
}

// A struct with a resizable ADT tail is not a frame object (C.2): its header's
// base is the struct's start, so tag matching reads the tag at an offset from it.
enum Value { A { items: i64[>..] }, B { n: i64 } }
struct Tagged { id: i64, v: Value.. }
fn byref_tail(t: Tagged&) {
    match t.v { A => print("ref tA ", t.id), B b => print("ref tB ", b.n, " ", t.id) }
}
fn byval_tail(t: Tagged) {
    match t.v { A => print("val tA ", t.id), B b => print("val tB ", b.n, " ", t.id) }
}
fn resizable_tail() {
    var t = Tagged { id: 3, v: Value.B { n: 4 } };
    match t.v { A => print("local tA ", t.id), B b => print("local tB ", b.n, " ", t.id) }
    byref_tail(t);
    byval_tail(Tagged { id: 3, v: Value.B { n: 4 } });
    var u = Tagged { id: 7, v: Value.A { items: [8] } };
    match u.v { A => print("local tA ", u.id), B b => print("local tB ", b.n, " ", u.id) }
    byref_tail(u);
    byval_tail(Tagged { id: 7, v: Value.A { items: [8] } });
}

fn main() {
    variable_field();
    resizable_tail();
}
