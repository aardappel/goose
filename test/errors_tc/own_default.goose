// error: default<own u64>() does not exist: own u64 is an own value with no empty state
// Only what produces an own value makes one.
fn main() { let h = default<own u64>(); }
