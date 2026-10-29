// runtime-debug: a height edited past what any span tree reaches aborts in the runtime's.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    let s = p.alloc_slice(2);
    p.free_slice(s);
    p.tree.height = 1000;
    p.free_slice(p.alloc_slice(1));
}
