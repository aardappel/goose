// error: function pick can fall off the end without returning value(s)
// A bare guard skips the rest of the function's body, which leaves its end
// without the value it has to return (§6.4, §7.3).
fn pick(c: bool) -> i64 {
    guard c;
    return 1;
}
fn main() { print(pick(true)); }
