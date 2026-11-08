// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes dst rooted differently from the cycle's entry call
// The back edge gives dst a node read out of run's stack, which points
// outside the cycle but is other storage than the entry call's, here the
// global g: a store into what dst points at would land in g in that
// activation, and keep a reference into run's tree after run returns, so
// dst is no storage the cycle may store into (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
var g = Node { first: null, next: null, n: 7 };
fn run() {
    var tree: Node[>..] = [];
    var out: Node&[>..<] = [];
    recursive fn link(dst: Node&, src: Node&, n: i64) {
        dst.first .= src;
        if n > 0 {
            for i in 0..out.len { link(out[i], src, n - 1); }
        }
    }
    let a = tree.push(Node { first: null, next: null, n: 0 });
    let b = tree.push(Node { first: null, next: null, n: 1 });
    out.push(g);
    link(a, b, 1);
}
fn main() {
    run();
    print(g.n);
}
