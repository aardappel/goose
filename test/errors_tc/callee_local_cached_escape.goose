// error: storing a reference rooted at text, which does not outlive the destination
// Ignoring a callee-local holder must retain stores through an actual
// caller-owned holder and reject a temporary that cannot outlive it.
import std;
struct Holder { text: const u8[:] }
fn store(h: Holder&, text: const u8[:]) {
    var r = Holder { text: text };
    h.text = r.text;
}
fn deep(h: Holder&) { store(h, str(7)); }
fn main() {
    var data: u8[>..] = [];
    data.append("kept");
    var h = Holder { text: "" };
    store(h, data[..]);
    deep(h);
    print(h.text);
}
