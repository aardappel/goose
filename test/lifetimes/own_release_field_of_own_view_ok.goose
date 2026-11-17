// The same view, of a field of a held handle whose element owns nothing,
// used only before the release: the consume judges the views still used,
// and this one's use is behind it.
import slot_pool;
struct Buf { data: u8[:] }
var bufs = slot_pool<Buf> {};
let bytes: u8[] = [0; 1024];
fn main() {
    let b = bufs.take(Buf { bytes[..32] });
    let r: u8[:] = b.data;
    print(r[..4]);
    bufs.release(b);
}
