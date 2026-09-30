// error: enum E<i64[>..]> is resizable (its variant B's payload is)
// error: the payload of its variant A holds self-relative references
// Whether an instance is resizable can depend on its type arguments: E<i64>
// is not, and binds A by reference, while E<i64[>..]> is.
enum E<T> { A { v: i64, p: i64&<u32>? }, B { t: T } }
fn main() {
    var ok: E<i64>.. = E<i64>.A { v: 1, p: null };
    match ok { A &a => print(a.v), B => {} }
    var bad: E<i64[>..]>.. = E<i64[>..]>.B { t: [1] };
    print(bad);
}
