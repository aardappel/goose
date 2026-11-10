// error: slot_pool.alloc_ref: an element that holds own values is allocated with take, whose handle release takes back
// The plain operations are for elements that own nothing.
import slice_pool;
import slot_pool;
struct Conn { id: i64, input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let c = conns.alloc_ref(Conn { id: 1, input: bytes.take_slice(4) });
    print(c.id);
}
