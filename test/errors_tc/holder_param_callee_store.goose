// error: cannot pop xs while box is still used: a reference into it was stored there
// put stores s into the Box its by-value holder parameter refers to: at the
// call that is a store of a slice of xs into box, which the holder's
// reference points at, not into the holder, which put received a copy of
// (§5.1, §9.2). box.p would read the popped slot that the push reuses.
struct Box { p: i64[:] }
struct H { b: Box& }
fn put(v: H, s: i64[:]) { let bb .= v.b; bb.p = s; }
fn main() {
    var zs: i64[3] = [0, 0, 0];
    var xs: i64[>..] = [1, 2, 3];
    var box = Box { p: zs[..] };
    put(H { b: box }, xs[..]);
    xs.pop();
    xs.push(77);
    print(box.p[2]);
}
