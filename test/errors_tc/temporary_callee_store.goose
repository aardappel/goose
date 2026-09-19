// error: storing a reference rooted at s, which does not outlive the destination
// A callee may keep a temporary argument only in its own locals, whichever
// call site checks it first: a specialization serves every call site whose
// arguments rank alike, and the second one here would keep a view of the
// loop's temporary past the loop.
struct Txt { body: u8[] }
struct H { s: const u8[:] }
fn put(h: H[>..]&, s: const u8[:]) { h.push(H { s: s }); }
fn mk() -> Txt[] { [Txt { body: "temp-data" }] }
fn main() {
    for t in mk() {
        var l: H[>..] = [];
        put(l, t.body[..]);
        print(l.len);
    }
    var outer: H[>..] = [];
    for t in mk() { put(outer, t.body[..]); }
    print(outer[0].s);
}
