// error: storing a reference into v, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// A reference into the grow-shrink array a variant type's payload holds may
// not be stored (§5.2), as one into a struct's tail may not.
enum Value { A { items: i64[>..<] }, B { n: i64 } }
struct H { r: i64& }
fn main() {
    var v: Value.A = Value.A { items: [1, 2] };
    let h = H { r: v.items[1] };
    v.items.pop();
    print(h.r);
}
