// error: returning a reference rooted in b, which dies with this function
// A function value's store into a holder of the function it is written in
// is on the holder's record (§7.6).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn twice<F>() { F(); F(); }
fn make() -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    twice() { x.link .= b; };
    return x;
}
fn main() { print(make().v); }
