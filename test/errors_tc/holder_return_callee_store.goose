// error: returning a reference rooted in b, which dies with this function
// A callee's store into the holder a reference parameter names is on the
// holder's record, as the caller's own store would be (§9.2).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn link(n: N&, b: B&) { n.link .= b; }
fn make() -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    link(x, b);
    return x;
}
fn main() { print(make().v); }
