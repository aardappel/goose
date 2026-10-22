// error: cannot assign w.v while r (bound at
// error: is still used: it may refer into w.v
// A struct's field of a variant type holds the grow-shrink array its payload
// does, which assigning the field whole shrinks (§5.2).
enum Value { A { items: i64[>..<] }, B { n: i64 } }
struct W { n: i64, v: Value.A }
fn main() {
    var w = W { n: 1, v: Value.A { items: [1, 2] } };
    let r = &w.v.items[1];
    w.v = Value.A { items: [5] };
    print(r);
}
