// error: cannot consume b while r is still used
// A slice read out of a field of a held handle answers to the handle even
// where the field is a plain one: the view was read through the handle, and
// the release frees the slot for its reuse, so the view dies with it. The
// field holds no own value and the release takes no block, and neither
// spares the view (§4.8 of docs/design/ownership.md, §5.5).
import slot_pool;
struct Buf { data: u8[:] }
var bufs = slot_pool<Buf> {};
let bytes: u8[] = [0; 1024];
fn main() {
    let b = bufs.take(Buf { bytes[..32] });
    let r: u8[:] = b.data;
    bufs.release(b);
    print(r[..]);
}
