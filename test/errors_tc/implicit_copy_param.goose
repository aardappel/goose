// error: xs is not fixed-size and is not copied implicitly
// error: pass copy(xs) for a copy, or bind it by reference
// A by-value non-fixed parameter takes an rvalue or copy(), not an lvalue (§4.1).
fn take(xs: i64[>..]) -> i64 { xs.len }
fn main() {
    var xs: i64[>..] = [1, 2];
    print(take(xs));
}
