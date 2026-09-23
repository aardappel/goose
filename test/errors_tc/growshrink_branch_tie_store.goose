// error: storing a reference that may point into G, which holds a grow-shrink array
// A string literal and a global tie at depth 0, and the branch value keeps
// the first branch's root, static data (§9.2). It may still be the view of
// the grow-shrink G the other branch gives, which is never stored (§5.2).
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }
var keep: K[>..] = [];

fn main() {
    let v = if keep.len == 0 { "abc" } else { G[..] };
    keep.push(K { s: v });
    G.clear();
    print(keep[0].s);
}
