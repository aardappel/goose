// error: what this return hands on is a view of h
// The claim releases the handle when the function exits; a view of the run
// handed to the caller reads stale bytes there (§5.5).
import slice_pool;
var bytes = slice_pool<u8> {};
fn peek(h: own u8[:]) -> u8[:] {
    let r: u8[:] = h;
    defer bytes.release(h);
    return r;
}
fn main() { print(peek(bytes.take_slice(8))[..]); }
