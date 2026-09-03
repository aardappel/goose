// Two locals in scope at the container's block can hold a Node, so the
// read-back names neither of them (§9.5).
struct Node { key: i32, next: Node&<u32>? }
fn main() {
    var pool: Node[>..] = [];
    pool.push(Node { key: 1 });
    block {
        var spare: Node[>..] = [];
        var slots: (Node&?)[>..] = [];
        spare.push(Node { key: 9 });
        slots.push(&pool[0]);
        let n = slots[0];
        assert(n);
        spare[0].next .= n;
    }
}
