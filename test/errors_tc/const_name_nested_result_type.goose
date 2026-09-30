// error: array capacity K names a local variable, not a constant
// A nested function's result type too is written where main's K is in
// scope (§3.3, §7.5).
let K = 3;

fn main() {
    let K = 5;
    fn g() -> i64[..K] { [1, 2] }
    print(g().cap, " ", K);
}
