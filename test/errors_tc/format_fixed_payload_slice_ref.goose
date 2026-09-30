// error: cannot write through this reference: non-writable provenance
// A fixed-mode ADT's payload is rendered from a copy (§3.7): a format hook
// taking a slice by reference is given the copy's slot, read-only (§9.5),
// not the one in the value, which it would re-point in place.
enum Shape {
    Circle { r: i64, s: i64[:] },
    Rect { w: i64, h: i64 },
}
var data: i64[>..] = [];
var other: i64[>..] = [];
var g: Shape = Shape.Rect { w: 0, h: 0 };
fn format(out: u8[>..]&, p: i64[:]&) { p = other[..]; format(out, p.len); }
fn main() {
    data.push(1);
    g = Shape.Circle { r: 5, s: data[..] };
    print(g);
}
