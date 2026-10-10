// error: returning a reference rooted in b, which dies with this function
// A local holder declared with a null link holds what is stored into it
// afterwards: here a reference to the function's own local (§9.2, §9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make() -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    x.link .= b;
    return x;
}
fn main() { print(make().v); }
