// error: contains self-relative references
import slot_pool;
struct Node { next: Node&<u32>? }
fn main() {
    var pool = slot_pool<Node> {};
    pool.alloc_index(Node { null });
}
