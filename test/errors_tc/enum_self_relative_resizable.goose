// error: enum R is resizable (its variant B's payload is), so a whole assignment may replace its variant
// error: and its payloads bind only by value
// error: the payload of its variant A holds self-relative references, which a copy does not keep
// error: make them plain or `in pool` references
// A resizable ADT binds its payloads by value only, and a copy does not keep
// self-relative references, so nothing but print could read A's link: the
// enum is rejected where it is declared.
struct H { x: i64, r: R.. }
enum R { A { v: i64, p: i64&<u32>? }, B { items: i64[>..] } }
fn main() {
    var h = H { x: 5, r: R.A { v: 1, p: null } };
    h.r = R.A { v: 1, p: h.x };
    print(h);
}
