// runtime-debug: an entry count edited past what a node holds aborts in the runtime's span tree.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    let s = p.alloc_slice(2);
    p.free_slice(s);
    p.nodes[p.tree.root].n = 40;
    p.alloc_slice(1);
}
