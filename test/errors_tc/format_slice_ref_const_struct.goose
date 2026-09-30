// error: cannot write through this reference: non-writable provenance
// A slice rendered as a field of the value printed lies in that value
// (§3.7): a format hook taking it by reference gets a reference as writable
// as the value, a read-only one for a `const` value (§9.5).
struct P { x: i64 }
struct S { f: P[:] }
var buf: P[>..] = [];
fn format(out: u8[>..]&, p: P[:]&) { out.append("P"); p = buf[1..]; }
fn main() {
    buf.push(P { x: 1 });
    buf.push(P { x: 2 });
    const c = S { f: buf[..] };
    print(c);
}
