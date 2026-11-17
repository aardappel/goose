// error: cannot take s while r is still used
// take moves the handle; the view handed to the caller's binding names the
// value the first variable held (docs/design/ownership.md 4.8). Once the
// moved handle is released the old view is stale; the checker keeps it
// simple and refuses it already at the move.
import slice_pool;
var bytes = slice_pool<u8> {};
fn main() {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    let m: own u8[:] = take(s);
    print(r[..]);
    bytes.release(m);
}
