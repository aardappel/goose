// error: cannot write through this reference: non-writable provenance
// A `for` binding of a reference element is as writable as the element's
// type says (§9.5), however writable the array: a `const u8&` element may
// lead into a string literal, which the write would change.
fn main() {
    let lit = "ab";
    var rs: (const u8&)[1] = [lit[0]];
    for &r in rs { r = 88; }
    print(lit);
}
