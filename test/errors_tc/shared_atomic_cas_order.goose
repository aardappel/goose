// error: invalid compare_exchange failure order
fn main() { let o, ok = shared_alloc(8); let a = shared_atomic_u64(o, 0, 0); atomic_compare_exchange(a, 0, 1, "Release", "Acquire"); }
