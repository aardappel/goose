// error: storing a view read from own handle s
// A view read from a handle lives in a variable, is passed down or
// returned, and is never stored (§5.5): the element would keep it past the
// release.
import slice_pool;
struct Holder { v: u8[:] }
var bytes = slice_pool<u8> {};
var hs: Holder[>..] = [];
fn main() {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    hs.push(Holder { v: r });
    print(r[..]);
    bytes.release(s);
    print(hs[0].v[..]);
}
