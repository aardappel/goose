// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes scratch rooted differently from the cycle's entry call
// The back edge passes node a reference read out of what scratch points
// at, which the back edge makes a local of the calling activation: that is
// no storage the cycle may store into, so what is read out of it may point
// anywhere, and node, resting on scratch, is pass-down only with it (§7.8).
struct Node { first: Node&?, next: Node&?, n: i64 }
fn main() {
    var tree: Node[>..] = [];
    var spare: Node[>..] = [];
    var out: Node&[>..<] = [];
    recursive fn eval(node: Node&, scratch: Node&, n: i64) {
        out.push(node);
        if n > 0 {
            var mine = Node { first: node, next: null, n: 9 };
            var k = scratch.first;
            if k != null { eval(k, mine, n - 1); }
        }
    }
    let root = tree.push(Node { first: null, next: null, n: 0 });
    let other = spare.push(Node { first: root, next: null, n: 1 });
    eval(root, other, 2);
    for o in out { print(o.n); }
}
