// error: storing a reference rooted at b, which does not outlive the destination
// A holder loaded whole through a reference to a local holds what was
// stored into that local (§9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn main() {
    var pool: N[>..] = [];
    {
        var b = B { v: 2.0 };
        var x = N { v: 1.0, link: b };
        let r: N& = x;
        pool.push(r);
    }
    print(pool.len);
}
