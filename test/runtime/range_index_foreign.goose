// runtime-debug: a nonempty foreign range is rejected at runtime.
fn locate(a: i64[>..]&, s: i64[:]) -> i64 { a.range_index(s) }
fn main() {
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [3, 4];
    print(locate(a, b[..]));
}
