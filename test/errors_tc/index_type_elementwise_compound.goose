// error: operator += cannot be applied to Node
// A compound update of a struct holding bound types computes plain integers
// in their places, which no bound type takes back (§3.11, §6.1).
struct Node { lo: u32 upto edges, hi: u32 upto edges, weight: i32 }
var edges: i64[>..] = [];
fn main() {
    for i in 4 { edges.push(i); }
    var n = Node { lo: 1 as (u32 upto edges), hi: 3 as (u32 upto edges), weight: 2 };
    n += n;
    for k in n.lo..n.hi { print(edges[k]); }
}
