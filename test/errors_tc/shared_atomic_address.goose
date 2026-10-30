// error: atomic_fetch_add requires SharedAtomicU64
fn main() { let address: u64 = 4096; atomic_fetch_add(address, 1, "Relaxed"); }
