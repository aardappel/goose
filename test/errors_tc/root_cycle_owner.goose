// error: recursive cycle while non-fixed-size local s
root struct S { a: i64[>..], b: i64[>..] }
recursive fn bad(n: i64) -> i64 {
    var s = S { .. };
    if n == 0 { return 0; }
    return bad(n - 1);
}
fn main() { print(bad(1)); }
