// The use of the view is before the release, and the release is last.
import slice_pool;
var bytes = slice_pool<u8> {};
fn main() {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    print(r[..]);
    bytes.release(s);
}
