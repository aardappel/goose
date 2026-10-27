// error: cannot call g: it may shrink t while x is still used, and x may refer into t
// A callee's shrink of the storage a reference parameter stands for, passed
// a reference read out of a struct, is of a value of the parameter's type
// somewhere behind it, whose grow-shrink array can hold what x views (§5.2).
// b.t refers to t or to t2, so b's contents span two roots (§9.5).
struct T { n: i64, items: i64[>..<] }
struct B { t: T&, r: u8[>..<] }
fn f(t: T&, x: i64&) {
    t.items.pop();
    t.items.push(9);
    print(x);
}
fn g(b: B&, x: i64&) { f(b.t, x); }
fn main() {
    var t = T { n: 0, items: [1, 2] };
    var t2 = T { n: 1, items: [3] };
    var b = B { t: t, r: [] };
    if t.n > 5 { b.t .= t2; }
    g(b, t.items[0]);
}
