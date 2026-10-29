// runtime-debug: an unknown slice origin is checked before touching the tree.
import slice_pool;
fn main() {
    var a = slice_pool<i64> {};
    var b = slice_pool<i64> {};
    let s = b.alloc_slice(4);
    a.alloc_slice(4);
    a.free_slice(s);
}
