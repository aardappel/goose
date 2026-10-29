// runtime-debug: a slice of another pool is refused before the tree changes.
import slice_pool;
fn main() {
    var a = slice_pool<i64> {};
    var b = slice_pool<i64> {};
    let s = b.alloc_slice(2);
    a.alloc_slice(3);
    a.realloc_slice(s, 4);
}
