// error: invalid memory order for atomic load
fn main() { let o, ok = shared_alloc(8); let a = shared_atomic_u64(o, 0, 0); atomic_load(a, "Release"); }
