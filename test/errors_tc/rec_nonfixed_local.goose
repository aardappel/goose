// No function in a recursive cycle may own non-fixed-size locals (§7.8).
recursive fn f(n: i64) -> i64 {
    var buf: i64[>..] = [];
    buf.push(n);
    if n <= 0 { 0 } else { f(n - 1) + buf.len }
}
fn main() {
    print(f(3));
}
