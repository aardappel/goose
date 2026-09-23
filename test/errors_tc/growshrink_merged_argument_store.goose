// error: storing a reference into s, which holds a grow-shrink array
// A parameter given a value that may point into a grow-shrink array may too
// (§5.2), whatever root the value kept: put may not store it.
struct H { f: u8[:] }
var related: u8[>..<] = [];
fn put(h: H&, s: u8[:]) { h.f = s; }
fn main() {
    related.push(65);
    var x: u8[2] = [66, 67];
    var h = H { f: x[..] };
    let c = x[0] > 0;
    put(h, if c { related[..] } else { x[..] });
    related.clear();
    related.push(90);
    print(h.f);
}
