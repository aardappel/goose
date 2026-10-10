// error: returning a reference rooted in b, which dies with this function
// A copy of one element of a local array holds what was stored into any of
// its elements: here a reference to the function's own local (§9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make() -> N {
    var b = B { v: 2.0 };
    var xs: N[2] = [N { v: 1.0, link: null }, N { v: 1.0, link: null }];
    xs[1].link .= b;
    let x = xs[0];
    return x;
}
fn main() { print(make().v); }
