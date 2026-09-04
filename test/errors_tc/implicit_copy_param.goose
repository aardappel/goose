// A by-value non-fixed parameter takes an rvalue or copy(), not an lvalue (§4.1).
fn take(xs: i64[>..]) -> i64 { xs.len }
fn main() {
    var xs: i64[>..] = [1, 2];
    print(take(xs));
}
