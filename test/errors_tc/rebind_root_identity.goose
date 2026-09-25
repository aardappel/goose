// error: a relative reference must point within the same root as its location (§3.9); this reference's root is not known exactly, only that it outlives poolA
// The read of `n` above sees what the rebind below leaves behind on the next
// iteration: `n` may then be in either pool, so it cannot be a relative
// link's target, which needs one root exactly (§3.9, §9.2).
struct Node { key: i32, next: Node&<u32>? }
var poolA: Node[>..] = [];
var poolB: Node[>..] = [];
fn main() {
    poolA.push(Node { key: 1 });
    poolA.push(Node { key: 2 });
    poolB.push(Node { key: 3 });
    var n: Node& = poolA[1];
    for i in 2 {
        poolA[0].next .= n;
        n .= poolB[0];
    }
}
