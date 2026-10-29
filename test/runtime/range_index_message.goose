// runtime-debug: a failed containment check aborts with the message it was given.
fn locate(a: i64[>..]&, s: i64[:]) -> i64 { a.range_index(s, "locate: not a run of a") }
fn main() {
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [3, 4];
    print(locate(a, a[1..]));
    print(locate(a, b[..]));
}
