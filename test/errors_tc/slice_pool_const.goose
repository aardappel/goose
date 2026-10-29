// error: read-only
import slice_pool;
fn no(p: const slice_pool<i64>&) { p.alloc_slice(1); }
fn main() { var p = slice_pool<i64> {}; no(p); }
