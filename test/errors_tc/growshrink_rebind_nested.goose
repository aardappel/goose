// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// keep's body was checked once, storing s while it pointed into no
// grow-shrink array; its second call would store the view of the
// grow-shrink `related` (§5.2).
struct H { f: u8[:] }
fn main() {
    var related: u8[>..<] = [];
    related.push(65);
    var x: u8[2] = [66, 67];
    var s = x[..];
    var h = H { f: x[..] };
    fn keep() { h.f = s; }
    keep();
    s = related[..];
    keep();
    related.clear();
    related.push(90);
    print(h.f);
}
