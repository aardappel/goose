// error: returning a reference rooted in pool.items
import slice_pool;
fn escape() -> i64[:] { var pool = slice_pool<i64> {}; pool.alloc_slice(2) }
fn main() { print(escape()); }
