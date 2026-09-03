// Two global containers can hold a Node, so a reference read back out of the
// slot array is rooted at the global scope, not at one of them (§9.5), and
// cannot be stored as a relative link (§3.9).
struct Node { key: i32, next: Node&<u32>? }
var pool: Node[>..] = [];
var spare: Node[>..] = [];
var slots: (Node&?)[>..] = [];
fn main() {
    pool.push(Node { key: 1 });
    spare.push(Node { key: 2 });
    slots.push(&pool[0]);
    let n = slots[0];
    assert(n);
    pool[0].next .= n;
}
