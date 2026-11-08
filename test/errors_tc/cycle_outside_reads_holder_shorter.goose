// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes v rooted differently from the cycle's entry call
// The back edge reads v off a pool handed to the cycle whose holders refer
// into `inner`, which dies before `keep`, where the cycle keeps v (§7.8,
// §9.2).
struct Node { first: Node&?, next: Node&?, n: i64 }
enum Val { Ref { node: Node& }, Num { v: i64 } }
recursive fn eval(keep: Val[>..<]&, out: Val[>..<]&, v: Val, n: i64) {
    keep.push(v);
    if n > 0 { for i in 0..out.len { eval(keep, out, out[i], n - 1); } }
}
fn main() {
    var tree: Node[>..] = [];
    var keep: Val[>..<] = [];
    let root = tree.push(Node { first: null, next: null, n: 0 });
    {
        var inner: Node[>..] = [];
        var out: Val[>..<] = [];
        out.push(Val.Ref { node: inner.push(Node { first: null, next: null, n: 5 }) });
        eval(keep, out, Val.Ref { node: root }, 1);
    }
    for k in keep { match k { Ref r => { print(r.node.n); }, _ => {} } }
}
