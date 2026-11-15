// error: cannot consume s while r is still used
// A view of a run read out of its own handle (§5.5): the release frees the
// run's slot for its reuse, and what still names it reads stale bytes after
// (docs/design/ownership.md 4.8).
import slice_pool;
var bytes = slice_pool<u8> {};
fn main() {
    let s = bytes.take_slice(8);
    let r: u8[:] = s;
    bytes.release(s);
    print(r[..]);
}
