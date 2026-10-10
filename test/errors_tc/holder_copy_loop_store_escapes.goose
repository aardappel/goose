// error: storing a reference rooted at b, which does not outlive the destination
// A copy read earlier in a loop body than a store into the array holds what
// that store put there in an earlier iteration (§9.2, §9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn main() {
    var pool: N[>..] = [];
    {
        var b = B { v: 2.0 };
        var xs: N[>..] = [];
        for i in 3 {
            if i == 2 { pool.push(xs[0]); }
            if i == 0 { xs.push(N { v: 1.0, link: null }); }
            if i == 1 { xs[0].link .= b; }
        }
    }
    print(pool.len);
}
