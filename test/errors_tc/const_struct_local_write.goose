// error: cannot assign through this path (const, or a read-only instantiation
// `const c: S = e` is a `let` of type `const S` (§4.4): a struct value fits
// the slot, whose contents stay read-only (§9.5).
struct S { n: i64 }
fn main() {
    const c: S = S { n: 1 };
    c.n = 2;
    print(c.n);
}
