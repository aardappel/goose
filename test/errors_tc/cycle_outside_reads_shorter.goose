// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes node rooted differently from the cycle's entry call
// The back edge reads node out of a pool handed to the cycle, which holds
// a reference into `inner`, but the entry call passed a node of `tree`,
// which outlives `inner`: what eval keeps for the outer block would
// outlive what it points at, so node is pass-down only (§7.8, §9.2).
struct Node { first: Node&?, next: Node&?, n: i64 }
recursive fn eval(keep: Node&[>..<]&, out: Node&[>..<]&, node: Node&, n: i64) {
    keep.push(node);
    if n > 0 {
        for i in 0..out.len { eval(keep, out, out[i], n - 1); }
    }
}
fn main() {
    var tree: Node[>..] = [];
    var keep: Node&[>..<] = [];
    let root = tree.push(Node { first: null, next: null, n: 0 });
    {
        var inner: Node[>..] = [];
        var out: Node&[>..<] = [];
        out.push(inner.push(Node { first: null, next: null, n: 5 }));
        eval(keep, out, root, 1);
    }
    for k in keep { print(k.n); }
}
