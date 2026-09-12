fn remove(a: i64[>..<]&) -> i64 { a.pop() }
fn main() {
    var other: i64[>..<] = [0];
    remove(other);
    var a: i64[>..<] = [0];
    print((a[..])[remove(a)]);
}
