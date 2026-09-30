// error: cannot assign through this path
// A path through a reference field of a `const` value writes what the
// reference reaches (§9.5), but the field itself is the const value's
// contents: it is not rebound.
struct S { n: i64 }
struct H { r: S& }
fn main() {
    var x = S { n: 1 };
    var y = S { n: 2 };
    const h = H { r: x };
    h.r.n = 7;
    h.r .= y;
    print(x.n, " ", h.r.n);
}
