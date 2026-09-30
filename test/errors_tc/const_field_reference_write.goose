// error: cannot assign through this path (const, or a read-only instantiation
// A field of a const type is assigned as a whole along its path (§4.4), but
// `&b.inner` reaches its contents, which are read-only (§9.5).
struct S { n: i64 }
struct Box { inner: const S }
fn main() {
    var b = Box { inner: S { n: 1 } };
    b.inner = S { n: 2 };
    let r = &b.inner;
    r.n = 3;
    print(b.inner.n);
}
