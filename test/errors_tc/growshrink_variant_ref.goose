// error: cannot pop v.items while r (bound at
// error: is still used: it may refer into v.items
// A variant type's payload holds a grow-shrink array as a struct's tail
// does: a shrink while a reference into it is in scope (§5.2), which the
// push after it would hand to another element.
enum Value { A { items: i64[>..<] }, B { n: i64 } }
fn main() {
    var v: Value.A = Value.A { items: [1, 2] };
    let r = &v.items[1];
    v.items.pop();
    v.items.push(99);
    print(r);
}
