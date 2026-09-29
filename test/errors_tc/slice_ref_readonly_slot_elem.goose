// error: cannot write through this reference: non-writable provenance
// An element reached through a `const T&` is not written (§9.5): bound by
// reference, as `&a[0]` of it is, it is a read-only reference.
var buf: u8[>..] = [];
fn set(q: u8[:]&) { q = buf[1..]; }
fn h(a: const (u8[:][>..])&) { set(a[0]); }
fn main() {
    buf.append("xyz");
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    h(arr);
    print(arr[0]);
}
