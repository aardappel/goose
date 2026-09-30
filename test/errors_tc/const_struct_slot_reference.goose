// error: storing a read-only reference of type const S& in a slot of type S&
// A const struct bound by reference is a `const S&`, which a slot of type
// S& does not take (§9.5).
struct S { n: i64 }
fn main() {
    const c: S = S { n: 1 };
    let r: S& = c;
    print(r.n);
}
