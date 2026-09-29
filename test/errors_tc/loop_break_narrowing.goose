// error: expected a value of type i64, got i64?
// A loop exits at its breaks, not where its body starts: o is narrowed at
// the head of every iteration, but not at the break after the rebind
// (§3.8, §6.4).
fn run(var o: i64?, c: bool) -> i64 {
    guard o else { return 0; }
    loop {
        if c { o .= null; }
        break;
    }
    return o;
}
fn main() { var x = 7; print(run(x, false)); print(run(x, true)); }
