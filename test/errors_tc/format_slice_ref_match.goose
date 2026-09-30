// error: cannot write through this reference: non-writable provenance
// A by-value `match` binding is a copy, never written (§6.5), and so are its
// fields: a format hook taking one by reference (§3.7) gets a read-only
// reference.
enum E { A { s: u8[:] }, B { k: i64 } }
var buf: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = buf[1..]; }
fn main() {
    buf.append("xyz");
    let e: E = E.A { s: buf[..] };
    match e { A a => { print(a.s); }, B b => {} }
}
