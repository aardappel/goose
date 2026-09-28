// error: cannot call drop, which shrinks a while w is still used: it may hold a reference or slice into it
// A guard's else block runs after its condition (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn first(a: i64[>..]&) -> i64 {
    let w = a[..];
    guard drop(a) != 0 else { return w[0]; }
    return 0;
}
fn main() {
    var a: i64[>..] = [1, 2];
    print(first(a));
}
