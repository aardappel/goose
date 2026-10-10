// error: returning a reference rooted in b, which dies with this function
// A return earlier in a loop body than a store into the holder sees what the
// store put there in an earlier iteration (§9.2).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make() -> N {
    var b = B { v: 2.0 };
    var x = N { v: 1.0, link: null };
    for i in 3 {
        if i == 2 { return x; }
        x.link .= b;
    }
    return N { v: 0.0, link: null };
}
fn main() { print(make().v); }
