// error: does not outlive the destination
import slot_pool;
struct Node { next: Node&<u32>? }
fn main() {
    var pool = slot_pool<Node> {};
    pool.alloc_with() {
        let local = Node {};
        Node { next: local }
    };
}
