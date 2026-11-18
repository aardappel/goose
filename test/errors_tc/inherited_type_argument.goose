// error: an explicit type argument cannot bind inherited parameter cb
// No explicit type argument is taken: the value is drawn by name (§7.6).
fn foo<inherited cb: fn()>() { cb(); }
fn main() {
    foo<i64>();
}
