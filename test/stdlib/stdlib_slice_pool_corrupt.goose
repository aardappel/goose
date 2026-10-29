// runtime-debug: edited bookkeeping cannot become an unchecked element address.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    let s = p.alloc_slice(2);
    p.free_slice(s);
    p.nodes[p.tree.root].data[0] = 99;
    p.alloc_slice(1);
}
