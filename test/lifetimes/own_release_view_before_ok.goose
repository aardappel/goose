// Views read out of own handles are ordinary values while the handle is
// held: they bind, pass down, and are used, and the defer's release runs at
// the block's end, after them. A use of one after its handle's release is
// the compile error that §4.8 of docs/design/ownership.md adds.
import slice_pool;
var bytes = slice_pool<u8> {};

// A view passed down as an argument is a use of it, not a store (§5.5).
fn takeit(x: u8[:]) { print(x[..]); }

fn main() {
    let s = bytes.take_slice(4);
    let r: u8[:] = s;
    takeit(r);                        // the view, still held
    defer bytes.release(s);
    print(r[..2]);                    // the defer releases at this block's end
}
