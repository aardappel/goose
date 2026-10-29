// error: contains self-relative references
import slice_pool;
struct Node { next: Node&<u32>? }
fn main() {
    var p = slice_pool<Node> {};
    let s = p.alloc_slice(2);
    p.realloc_slice(s, 3);
}
