// error: call f stores a reference rooted at tree through its parameter dst, whose argument points into g, which tree does not outlive
// tree is a local of run, outside the cycle, so it outlives every
// activation, but not the global g that the back edge gives dst: the store
// would leave g.first pointing into tree after run returns. The body was
// checked for the entry call's root, so the back edge checks what it
// passes against the body's stores (§7.8, §9.2).
struct Node { first: Node&?, n: i64 }
var g = Node { first: null, n: 7 };
fn run() {
    var tree: Node[>..] = [];
    tree.push(Node { first: null, n: 42 });
    let root = tree.push(Node { first: null, n: 1 });
    recursive fn f(dst: Node&, n: i64) {
        dst.first .= tree[0];
        if n > 0 { f(g, n - 1); }
    }
    f(root, 1);
}
fn main() {
    run();
    var k = g.first;
    if k != null { print(k.n); }
}
