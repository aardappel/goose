// error: call f stores a reference rooted at pool through its parameter dst, whose argument points into g, which pool does not outlive
// A pool handed to the cycle outlives every activation, but not the global
// g that the back edge gives dst: the store would leave g.first pointing
// into run's pool after run returns (§7.8, §9.2).
struct Node { first: Node&?, n: i64 }
var g = Node { first: null, n: 7 };
recursive fn f(pool: Node[>..]&, dst: Node&, n: i64) {
    dst.first .= pool[0];
    if n > 0 { f(pool, g, n - 1); }
}
fn run() {
    var tree: Node[>..] = [];
    tree.push(Node { first: null, n: 42 });
    let root = tree.push(Node { first: null, n: 1 });
    f(tree, root, 1);
}
fn main() {
    run();
    var k = g.first;
    if k != null { print(k.n); }
}
