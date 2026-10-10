// error: returning a reference rooted in b, which dies with this function
// What a callee stored into a local array through a reference parameter is
// on the array's record, which a copy of an element holds (§9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn fill(xs: N[>..]&, b: B&) { xs.push(N { v: 1.0, link: b }); }
fn make() -> N {
    var b = B { v: 2.0 };
    var xs: N[>..] = [];
    fill(xs, b);
    let x = xs[0];
    return x;
}
fn main() { print(make().v); }
