// runtime-debug: a negative length aborts at the allocation that asks for it.
import slice_pool;
fn main() {
    var p = slice_pool<i64> {};
    p.alloc_slice(2);
    p.alloc_slice(-2);
}
