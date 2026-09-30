// error: cannot write through this reference: non-writable provenance
// A field reached through a `const S&` is not written (§9.5): a format hook
// taking it by reference (§3.7) gets a read-only reference, as `&c.f` is.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = buf[1..]; }
fn show(c: const S&) { print(c.f); }
fn main() {
    buf.append("xyz");
    var s = S { f: buf[..] };
    show(s);
}
