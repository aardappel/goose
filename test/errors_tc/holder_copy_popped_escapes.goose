// error: storing a reference rooted at b, which does not outlive the destination
// A popped element holds what was stored into its array: a reference to a
// local of the block, which the outer pool outlives (§9.2, §9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn main() {
    var pool: N[>..] = [];
    {
        var b = B { v: 2.0 };
        var xs: N[>..<] = [];
        xs.push(N { v: 1.0, link: b });
        pool.push(xs.pop());
    }
    print(pool.len);
}
