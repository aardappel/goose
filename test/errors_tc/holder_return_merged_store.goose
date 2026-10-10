// error: returning a reference rooted in b, which dies with this function
// A store through a reference that may name either of two holders is on
// both their records (§9.2).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make(c: bool) -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    var y = N { v: 1.0, link: null };
    let r: N& = if c { x } else { y };
    r.link .= b;
    return y;
}
fn main() { print(make(true).v); }
