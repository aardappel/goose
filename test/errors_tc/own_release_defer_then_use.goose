// error: cannot release s while r is still used
// The defer's claim releases at the inner block's end; an outer view named
// the run until then.
import slice_pool;
var bytes = slice_pool<u8> {};
fn go() {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    {
        defer bytes.release(s);
        print(r[..]);
    }
    print(r[..]);
}
fn main() { go(); }
