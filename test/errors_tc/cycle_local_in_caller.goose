// error: outer calls into its recursive cycle while non-fixed-size local names
// The back edge is in inner, but outer's call to inner is a call into the
// cycle too, and names is in scope across it (§7.8).
recursive fn outer(n: i64) -> i64 {
    var names: u8[][>..] = [];
    names.push("x");
    return inner(n) + names.len;
}
fn inner(n: i64) -> i64 {
    if n == 0 { return 0; }
    return outer(n - 1);
}
fn main() { print(outer(3)); }
