// error: cannot assign through this path (const, or a read-only instantiation
// `&c` of a const struct is a `const S&` (§9.5), whatever c was given.
struct S { n: i64 }
fn main() {
    let s = S { n: 1 };
    const c: S = s;
    let r = &c;
    r.n = 2;
    print(c.n);
}
