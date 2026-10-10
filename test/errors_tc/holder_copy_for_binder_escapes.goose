// error: storing a reference rooted at b, which does not outlive the destination
// A `for` binder's copy of an element holds what was stored into the array
// it walks (§9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn main() {
    var pool: N[>..] = [];
    {
        var b = B { v: 2.0 };
        var xs: N[>..] = [];
        xs.push(N { v: 1.0, link: b });
        for x in xs { pool.push(x); }
    }
    print(pool.len);
}
