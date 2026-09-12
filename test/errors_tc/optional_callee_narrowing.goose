// Where a callee's body ends narrows nothing for its caller: an early return
// never gets there.
var g: i64? = null;
fn check() { if !g { return; } }
fn main() {
    check();
    print(g + 1);
}
