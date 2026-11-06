// runtime-debug: a tree whose child leads back to a node already visited aborts trim's walk.
import slice_pool;
fn main() {
    var p = slice_pool<u8> {};
    let s = p.alloc_slice(4);
    p.free_slice(s);
    p.tree.height = 1;
    p.nodes[p.tree.root].data[40] = p.tree.root;
    p.trim();
}
