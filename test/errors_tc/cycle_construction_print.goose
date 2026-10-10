// error: trace calls into its recursive cycle inside the arguments of print(...), whose text is under construction
// print renders the whole line before writing any of it, on a data stack of
// the activation, so the call into the cycle among its arguments holds that
// stack (§7.8): bind the result first, then print it.
recursive fn trace(n: i64) -> i64 {
    if n == 0 { return 0; }
    print("level ", n, " below ", trace(n - 1));
    return n;
}
fn main() { print(trace(3)); }
