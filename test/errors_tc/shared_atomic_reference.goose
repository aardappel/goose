// error: atomic_load requires SharedAtomicU64
fn main() { var n: u64 = 0; atomic_load(&n, "Relaxed"); }
