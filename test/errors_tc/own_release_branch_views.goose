// error: cannot consume s1 while r is still used
// Views bound in both branches: whichever ran, r may name the released
// handle's run, and the consume of either handle is refused while the view
// is used (§5.5).
import slice_pool;
var bytes = slice_pool<u8> {};
var src: u8[4] = ['a', 'b', 'c', 'd'];
fn go(cond: bool) {
    let s1 = bytes.take_slice(4);
    let s2 = bytes.take_slice(4);
    var r: u8[:] = src[..0];
    if cond {
        r = s1[..];
    } else {
        r = s2[..];
    }
    bytes.release(s1);
    bytes.release(s2);
    print(r[..]);
}
fn main() { go(false); }
