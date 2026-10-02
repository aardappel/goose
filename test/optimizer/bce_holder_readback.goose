// runtime-debug: a reference read out of a holder may be what was stored there last.
// h.r is rebound to b where b is not empty, so r may be b, and the pops
// through it may empty b: the index after them keeps its check (§9.5).
struct H { r: i64[>..<]& }
fn main() {
    var a: i64[>..<] = [1, 2, 3, 4];
    var b: i64[>..<] = [5, 6, 7, 8];
    var h = H { r: a };
    if b.len > 0 { h.r .= b; }
    let r = h.r;
    var t = b[3];
    r.pop();
    r.pop();
    r.pop();
    r.pop();
    t += b[3]; // bce:keep
    print(t);
}
