// error: cannot write through this reference: non-writable provenance
// `&` of a location reached through a `const T&` field is a `const` reference,
// out of a `const` holder as out of a `var` one (§9.5).
struct S { n: i64 }
struct H { r: const S& }
fn inc(p: i64&) { p = p + 1; }
fn main() {
    var x = S { n: 1 };
    const h = H { r: x };
    let m = &h.r.n;
    inc(m);
    print(x.n);
}
