// runtime-debug: negative realloc lengths abort before changing the tree.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    let s = p.alloc_slice(2);
    p.realloc_slice(s, -1);
}
