// error: cannot pop v.items while r (bound at
// error: is still used: it may refer into v.items
// A parameter's class holds the grow-shrink array its argument's variant
// payload does (§5.2): the body's shrink meets the reference it took.
enum Value { A { items: i64[>..<] }, B { n: i64 } }
fn f(v: Value.A&) {
    let r = &v.items[1];
    v.items.pop();
    print(r);
}
fn main() {
    var v: Value.A = Value.A { items: [1, 2] };
    f(v);
}
