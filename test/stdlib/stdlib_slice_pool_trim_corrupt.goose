// runtime-debug: a free span edited past the pool's length aborts trim before it discards anything.
import slice_pool;
fn main() {
    var p = slice_pool<u8> {};
    let s = p.alloc_slice(4);
    p.free_slice(s);
    p.nodes[p.tree.root].data[31] = p.items.len * 2;
    p.trim();
}
