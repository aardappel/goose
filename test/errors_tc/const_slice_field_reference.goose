// error: cannot write through this reference: non-writable provenance
// `&` of a slice field of a `const` value is a `const` reference to the
// slot (§9.5), which cannot re-point it, whatever the slice's elements allow.
struct HS { s: i64[:] }
fn main() {
    var a = [1, 2, 3];
    const hs = HS { s: a[..] };
    hs.s[0] = 7;
    let q = &hs.s;
    q = a[1..];
    print(hs.s);
}
