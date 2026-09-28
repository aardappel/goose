// error: cannot call drop, which shrinks a: a[0], evaluated earlier in the statement
// error: may still refer into a
// A literal's fields are built in order: the reference field is in place
// when the next one clears the array it points into (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
struct S { r: i64&, n: i64 }
fn main() {
    var a: i64[>..] = [1, 2];
    let s = S { r: a[0], n: drop(a) };
    print(s.r);
}
