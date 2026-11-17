// error: cannot consume h while part is still used
// A slice of a slice: the view read through the first one answers to the
// same handle, so the consume finds it as well (§5.5).
import slice_pool;
var bytes = slice_pool<u8> {};
fn main() {
    let h = bytes.take_copy("hello");
    let whole: u8[:] = h;
    let part = whole[1..];
    bytes.release(h);
    print(part.len);
}
