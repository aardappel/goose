// error: cannot clear a while r is still used
// A function no call reaches is checked with an array of its own behind each
// reference parameter (lifetimes/unreached_param_classes.goose), so a real
// hazard in it is found as it would be for any call.
fn drop_all(a: i64[>..]&) { let r = &a[0]; a.clear(); print(r); }
fn main() { print(1); }
