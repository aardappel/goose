// error: cannot consume h while r is still used
// The same view derived through a borrow bound from the handle; the
// consume of the handle finds it as well.
import slice_pool;
import slot_pool;
struct Conn { input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let h = conns.take(Conn { input: bytes.take_slice(8) });
    let c: Conn& = h;
    let r: u8[:] = c.input;
    conns.release(h) { bytes.release(take(it.input)); };
    print(r[..]);
}
