// error: cannot write through this reference: non-writable provenance
// A by-value `for` binding of a reference element binds the reference it
// holds, as writable as the element's type says (§9.5): through a `const S&`
// element it cannot write the `const` global the element leads to.
struct S { n: i64 }
const G = S { n: 1 };
fn inc(p: i64&) { p = p + 1; }
fn main() {
    var rs: (const S&)[1] = [G];
    for r in rs { inc(r.n); }
    print(G.n);
}
