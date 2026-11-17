// error: cannot consume s while r is still used
// The release in one branch; the view outlives the join.
import slice_pool;
var bytes = slice_pool<u8> {};
fn go(cond: bool) {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    if cond {
        bytes.release(s);
    } else {
        print(r[..]);
        bytes.release(s);
    }
    print(r[..]);
}
fn main() { go(false); go(true); }
