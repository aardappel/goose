// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// r refers to the slice variable s, whose binding the checker had when r was
// bound; after the rebind, what r loads is the view of the grow-shrink
// `related` (§5.2).
struct H { f: const u8[:] }
fn main() {
    var related: u8[>..<] = [];
    related.push(65);
    var x: u8[2] = [66, 67];
    var s = x[..];
    let r: u8[:]& = s;
    s = related[..];
    var h = H { f: x[..] };
    h.f = r;
    related.clear();
    related.push(90);
    print(h.f);
}
