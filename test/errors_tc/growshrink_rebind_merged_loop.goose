// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// The shrink at the top of the loop was checked while s pointed into no
// grow-shrink array, and a loop body is checked once: the next round's
// shrink would leave s viewing the cleared array (§5.2).
var related: u8[>..<] = [];
fn main() {
    related.push(67);
    var x: u8[2] = [65, 66];
    var s = x[..];
    let c = x[0] > 0;
    for i in 2 {
        related.clear();
        related.push(88);
        print(s);
        s = if c { related[..] } else { x[..] };
    }
}
