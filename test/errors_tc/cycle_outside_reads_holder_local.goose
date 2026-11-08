// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes v rooted differently from the cycle's entry call
// The back edge packs a local of the calling activation into the holder it
// passes, so what v refers to does not lie outside the cycle in every
// activation, and v is not stored there (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
enum Val { Ref { node: Node& }, Num { v: i64 } }
fn main() {
    var tree: Node[>..] = [];
    var out: Val[>..<] = [];
    recursive fn eval(v: Val, n: i64) {
        out.push(v);
        if n > 0 {
            var mine = Node { first: null, next: null, n: 9 };
            for i in 0..out.len { eval(out[i], n - 1); }
            eval(Val.Ref { node: mine }, n - 1);
        }
    }
    let root = tree.push(Node { first: null, next: null, n: 0 });
    eval(Val.Ref { node: root }, 2);
    for o in out { match o { Ref r => { print(r.node.n); }, _ => {} } }
}
