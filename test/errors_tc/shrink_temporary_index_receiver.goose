// error: cannot call remove, which shrinks a: the elements of a[..], which the statement reads after it
// error: may be in a
fn remove(a: i64[>..<]&) -> i64 { a.pop() }
fn main() {
    var other: i64[>..<] = [0];
    remove(other);
    var a: i64[>..<] = [0];
    print((a[..])[remove(a)]);
}
