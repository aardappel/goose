// error: storing a read-only reference of type u8[:]& (read-only in this instantiation) in a slot of type u8[:]&
// A result declared a reference is as writable as what the function returns
// (§9.5): a field reached through a `const S&`, bound by reference, is a
// read-only reference, so get's result is read-only at this call.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn get(c: const S&) -> u8[:]& { return c.f; }
fn main() {
    buf.append("xyz");
    var s = S { f: buf[..] };
    let r: u8[:]& = get(s);
    r = buf[1..];
    print(s.f);
}
