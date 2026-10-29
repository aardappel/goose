// error: a relative reference must point within the same root as its location
import slot_pool;
struct Node { next: Node&<u32>? }
fn both<F>(a: slot_pool<Node>&, b: slot_pool<Node>&) {
    a.alloc_with(F);
    b.alloc_with(F);
}
fn main() {
    var a = slot_pool<Node> {};
    var b = slot_pool<Node> {};
    let first .= a.alloc_with() { Node {} };
    both(a, b) { Node { next: first } };
}
