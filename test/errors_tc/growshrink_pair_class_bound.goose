// error: cannot call g: it may shrink t while x is still used, and x may refer into t (§5.2)
// A callee's shrink of the storage a reference parameter stands for, passed
// a reference read out of a struct, is of a value of the parameter's type
// somewhere behind it, whose grow-shrink array can hold what x views (§5.2).
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
    var b = B { t: t, r: [] };
    g(b, t.items[0]);
}
