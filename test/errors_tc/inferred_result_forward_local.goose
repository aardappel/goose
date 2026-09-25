// error: returning a reference rooted in t, which dies with this function
// A return forwarding every result of a multi-value call keeps them as they
// are where the result types are inferred (§7.1), so a reference rooted in
// the function's own local is returned as one, which its root forbids (§9.2).
fn both(a: i64[>..]&, b: i64[>..]&) -> i64[>..]&, i64[>..]& { return a, b; }
fn pair() {
    var t: i64[>..] = [1];
    var u: i64[>..] = [2];
    return both(t, u);
}
fn main() {
    let x, y = pair();
    print(x, y);
}
