// error: sum calls into its recursive cycle with the non-fixed-size temporary make(n) as an argument
// A call's resizable result is a temporary of the statement; passed to the
// cycle as a slice, it is held across the call (§7.8). A slice of scratch
// owned outside the cycle, or static text, costs the recursion nothing.
fn make(k: i64) -> i64[>..] { return [k, k + 1]; }
recursive fn sum(s: const i64[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    return s[0] + sum(make(n), n - 1);
}
fn main() { print(sum(make(1), 3)); }
