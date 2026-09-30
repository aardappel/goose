// error: cannot assign through this path (const, or a read-only instantiation
// A `const` field of a struct type takes a plain struct value at
// construction, and its fields stay read-only (§3.2, §9.5).
struct S { n: i64 }
struct Box { const inner: S }
fn main() {
    let s = S { n: 1 };
    var b = Box { inner: s };
    b.inner.n = 2;
    print(b.inner.n);
}
