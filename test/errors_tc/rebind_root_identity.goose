// A loop body is checked once, so the read of `n` above sees what the rebind
// below leaves behind on the next iteration: the rebind may not move `n` to
// another array once its root has been used as a relative link's identity
// (§3.9, §9.2).
struct Node { key: i32, next: Node&<u32>? }
var poolA: Node[>..] = [];
var poolB: Node[>..] = [];
fn main() {
    poolA.push(Node { key: 1 });
    poolA.push(Node { key: 2 });
    poolB.push(Node { key: 3 });
    var n: Node& = &poolA[1];
    for i in 2 {
        poolA[0].next .= n;
        n .= &poolB[0];
    }
}
