// error: still under construction
import slot_pool;
struct Node { me: Node&<u32> }
fn main() {
    var pool = slot_pool<Node> {};
    pool.alloc_with() {
        pool.items.push(Node { me: self });
        Node { me: self }
    };
}
