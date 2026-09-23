// error: storing a reference that may point into related, which holds a grow-shrink array
// A function's result carries what its returns may point into (§9.2): pick
// is rooted at xs, a global, but may return the view of the grow-shrink
// `related`, which is never stored (§5.2).
struct H { f: u8[:] }
var related: u8[>..<] = [];
var xs: u8[2] = [66, 67];
fn pick(c: bool) -> u8[:] {
    return if c { xs[..] } else { related[..] };
}
fn main() {
    related.push(65);
    var h = H { f: xs[..] };
    h.f = pick(related.len > 5);
    related.clear();
    related.push(90);
    print(h.f);
}
