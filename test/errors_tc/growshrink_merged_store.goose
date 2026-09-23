// error: storing a reference that may point into related, which holds a grow-shrink array
// A branch's value is rooted at the deeper of its branches' roots (§9.2), x
// here, but may still be the view of the grow-shrink `related` the other
// branch gives, which is never stored (§5.2): no shrink of related looks at
// h, which would read the cleared array.
struct H { f: const u8[:] }
var related: u8[>..<] = [];
fn main() {
    related.push(65);
    var x: u8[2] = [66, 67];
    var c = x[0] > 3;
    let s = if c { related[..] } else { x[..] };
    var h = H { f: "z" };
    h.f = s;
    related.clear();
    print(h.f);
}
