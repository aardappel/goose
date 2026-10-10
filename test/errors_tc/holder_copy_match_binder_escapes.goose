// error: storing a reference rooted at b, which does not outlive the destination
// A `match` binder's copy of a payload holds what was stored into the array
// the scrutinee lies in (§9.5).
struct B { v: f64 }
struct N { v: f64, link: B? }
enum E { None, Some { n: N } }
fn main() {
    var pool: N[>..] = [];
    {
        var b = B { v: 2.0 };
        var es: E[>..] = [];
        es.push(E.Some { n: N { v: 1.0, link: b } });
        for e in es {
            match e {
                Some s => { pool.push(s.n); },
                None => {},
            }
        }
    }
    print(pool.len);
}
