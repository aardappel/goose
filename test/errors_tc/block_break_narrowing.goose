// error: expected a value of type i64, got i64?
// o is narrowed where the block's body ends, but not at the break, which
// the rebind to null reaches (§3.8, §6.4).
fn run(var o: i64?, c: bool) -> i64 {
    guard o else { return 0; }
    block {
        if c { o .= null; break; }
    }
    return o;
}
fn main() { var x = 7; print(run(x, false)); print(run(x, true)); }
