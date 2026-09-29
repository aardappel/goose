// error: expected a value of type i64, got i64?
// A while exits where its condition is false and at each break: o is
// narrowed at the one, not at the other (§3.8, §6.4).
fn run(var o: i64?, c: bool) -> i64 {
    guard o else { return 0; }
    var i = 0;
    while i < 3 {
        i++;
        if c { o .= null; break; }
    }
    return o;
}
fn main() { var x = 7; print(run(x, false)); print(run(x, true)); }
