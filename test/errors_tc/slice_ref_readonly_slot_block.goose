// error: cannot write through this reference: non-writable provenance
// A block's parameter binds its argument as a function's does (§4.1, §7.6):
// a field reached through a `const S&` is a read-only reference.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn run<F>(c: const S&) { F(c.f); }
fn main() {
    buf.append("xyz");
    var s = S { f: buf[..] };
    run(s) { p: u8[:]& => p = buf[1..]; };
    print(s.f);
}
