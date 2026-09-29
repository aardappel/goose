// error: operator + cannot be applied to i64? and i64
// A for exits after its last iteration and at each break: o is narrowed at
// the one, not at the other (§3.8, §6.5).
fn run(var o: i64?, c: bool) -> i64 {
    guard o else { return 0; }
    for i in 3 {
        if c && i == 1 { o .= null; break; }
    }
    return o + 1;
}
fn main() { var x = 7; print(run(x, false)); print(run(x, true)); }
