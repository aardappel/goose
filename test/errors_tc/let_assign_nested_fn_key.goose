// error: cannot assign to let x, which may be assigned already
// The second call finds x unassigned, as the first did, but maybe assigned,
// which the first did not: init is checked again for it, and assigns the let
// a second time (§4.4, §7.5).
fn run(c: bool) {
    let x: i64;
    fn init() { x = 5; }
    if c { init(); }
    init();
    print(x);
}
fn main() { run(true); }
