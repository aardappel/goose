// error: cannot assign through this path
// A slice loaded through a reference to a slice is no more writable than the
// reference (§9.5): through a `const (T[:])&` field it is read-only, though
// the slice variable the reference names holds a writable slice.
struct HR { rs: const (i64[:])& }
fn main() {
    var a = [1, 2, 3];
    var s = a[..];
    var hr = HR { rs: s };
    hr.rs[0] = 7;
    print(a);
}
