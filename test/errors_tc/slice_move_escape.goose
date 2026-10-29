// error: does not outlive the destination
var saved: i64[:][1] = [default<i64[:]>(); 1];
fn move<T>(dst: T[:], src: T[:]) { dst.move_from(src); }
fn main() {
    var local = [1, 2];
    var holders = [local[..]];
    move(saved[..], holders[..]);
}
