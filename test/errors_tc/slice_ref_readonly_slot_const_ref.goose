// error: cannot write through this reference: non-writable provenance
// A field reached through a `const S&` is not written (§9.5): bound by
// reference, as `&c.f` of it is, it is a read-only reference, however
// writable the slice it holds.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn set(q: u8[:]&) { q = buf[1..]; }
fn h(c: const S&) { set(c.f); }
fn main() {
    buf.append("xyz");
    var s = S { f: buf[..] };
    h(s);
    print(s.f);
}
