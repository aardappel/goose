// error: a relative reference in bytes must point into bytes
// error: view is a global var, which any function may bind, and may point into bytes or more
// A global slice is bound by `=` from any function, as a reference is by
// `.=`: mark, checked while view is bytes[..], is called again after move
// has pointed it into more (§9.5).
var bytes: u8[>..] = [1, 2, 3];
var more: u8[>..] = [4, 5, 6];
var view: u8[:] = bytes[..];
struct B { at: u8&<u16 in bytes> }
var marks: B[>..] = [];
fn mark() { marks.push(B { at: view[1] }); }
fn move() { view = more[1..]; }
fn main() {
    mark();
    move();
    mark();
}
