// error: memory order must be a compile-time string
fn load(a: SharedAtomicU64, order: u8[:]) -> u64 { atomic_load(a, order) }
fn main() { let o, ok = shared_alloc(8); let a = shared_atomic_u64(o, 0, 0); print(load(a, "Relaxed")); }
