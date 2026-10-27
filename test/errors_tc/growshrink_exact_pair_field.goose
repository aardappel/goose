// error: cannot pop t.items while x
// error: is still used: it may refer into t.items
// b's one reference is to t, so b is bound to t exactly (§9.2), and g
// passes b.t on as t itself: f's t is main's t and x lies in t.items, so
// f's own body rejects popping t.items while x is still used (§5.2). An
// element of another T's items as x would leave the call clean.
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
