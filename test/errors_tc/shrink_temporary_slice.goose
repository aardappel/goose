fn read(r: i64[:], unused: i64) { print(r); }
fn remove(a: i64[>..<]&) -> i64 { a.pop() }
fn main() {
    var a: i64[>..<] = [42];
    read(a[..], remove(a));
}
