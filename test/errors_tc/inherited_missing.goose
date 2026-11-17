// error: inherited parameter cb: no function named cb is in scope at the call (§7.6)
// The value is drawn by name from the scope of the call, where no `cb` is
// declared (§7.6).
fn foo<inherited cb: fn()>() { cb(); }
fn main() {
    foo();
}
