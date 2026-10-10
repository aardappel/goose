// error: returning a reference rooted in b, which dies with this function
// A nested function's store into a holder of its parent's is on the
// holder's record (§7.5, §9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make() -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    fn set() { x.link .= b; }
    set();
    return x;
}
fn main() { print(make().v); }
