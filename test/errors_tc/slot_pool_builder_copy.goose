// error: contains self-relative references, is not supported
import slot_pool;
struct Node { me: Node&<u32> }
fn main() {
    var pool = slot_pool<Node> {};
    let first .= pool.alloc_with() { Node { me: self } };
    pool.alloc_with() { first };
}
