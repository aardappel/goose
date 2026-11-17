// A view variable rebound to another handle stops answering to the old one;
// the release of one handle leaves the other handle's views alone.
import slice_pool;
import slot_pool;
struct Conn { input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let h = conns.take(Conn { input: bytes.take_slice(8) });
    let g = conns.take(Conn { input: bytes.take_slice(8) });
    var r: u8[:] = h.input;
    r = g.input;
    conns.release(h) { bytes.release(take(it.input)); };
    print(r[..2]);
    conns.release(g) { bytes.release(take(it.input)); };
}
