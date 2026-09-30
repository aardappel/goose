// error: cannot write through this reference: non-writable provenance
// A slice that is no storage, such as a call's result, is rendered from a
// temporary (§3.7): a format hook taking it by reference gets a read-only
// reference, as a view into a temporary is (§9.5).
var g: u8[>..] = [];
var h: u8[>..] = [];
var gs: u8[:] = h[..];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn get() -> u8[:] { return gs; }
fn main() {
    g.append("xyz");
    h.append("abcd");
    gs = h[..];
    print(get());
    print(gs.len);
}
