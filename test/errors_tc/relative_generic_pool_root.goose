// error: a relative reference in pool must point into pool
// error: this reference is rooted at v
// A `T` local over an array of `in pool` links holds the plain reference
// (§3.9); storing it into a link needs it to point into the pool, and this
// one, from v, points into spare.
struct P { v: i64 }
var pool: P[>..] = [];
var spare: P[>..] = [];
fn put_first<T>(xs: T[:], v: T) {
    let t: T = v;
    xs[0] .= t;
}
fn main() {
    pool.push(P { v: 1 });
    spare.push(P { v: 2 });
    var links: (P&<u32 in pool>)[>..] = [];
    links.push(pool[0]);
    put_first(links, &spare[0]);
}
