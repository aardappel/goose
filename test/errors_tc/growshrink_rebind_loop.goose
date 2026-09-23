// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// The store of s into h was checked while s pointed into no grow-shrink array;
// the loop's next round stores it again after the rebind, which may not
// start pointing into one (§5.2).
struct H { f: u8[:] }
fn main() {
    var related: u8[>..<] = [];
    related.push(65);
    var x: u8[2] = [66, 67];
    var y: u8[2] = [1, 2];
    let c = x[0] > 0;
    var s = if c { x[..] } else { y[..] };
    var h = H { f: y[..] };
    for i in 2 {
        h.f = s;
        s = related[..];
    }
    related.clear();
    related.push(90);
    print(h.f);
}
