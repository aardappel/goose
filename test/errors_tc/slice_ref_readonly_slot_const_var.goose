// error: cannot write through this reference: non-writable provenance
// A variable of a `const` type is assigned by its own name alone (§9.5):
// `&t` is a read-only reference, and so is t bound by reference, whatever
// slice it was assigned.
var buf: u8[>..] = [];
fn skip(q: (const u8[:])&) { q = q[1..]; }
fn main() {
    buf.append("xyz");
    var t: const u8[:];
    t = buf[..];
    skip(t);
    print(t);
}
