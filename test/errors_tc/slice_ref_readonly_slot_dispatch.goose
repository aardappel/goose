// error: cannot write through this reference: non-writable provenance
// A dispatched call binds its other arguments as any call does (§4.1, §7.1):
// a field reached through a `const S&` is a read-only reference in each case.
enum E { A { k: i64 }, B { k: i64 } }
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn set(e: E.A, q: u8[:]&) { q = buf[1..]; }
fn set(e: E.B, q: u8[:]&) { q = buf[2..]; }
fn h(c: const S&, e: E) { set(e, c.f); }
fn main() {
    buf.append("xyz");
    var s = S { f: buf[..] };
    h(s, E.A { k: 1 });
    print(s.f);
}
