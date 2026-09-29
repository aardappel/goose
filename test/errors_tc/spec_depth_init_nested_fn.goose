// error: storing a reference rooted at t, which does not outlive the destination
// A function declared in a global initializer compares its arguments' roots
// with the initializer's locals it names: put may store a view of t in mine,
// declared after t, but not in hs, which outlives t. Only the depths tell the
// two calls apart; t reads the same at both.
struct H { s: const u8[:] }
let g = block {
    var hs: H[>..] = [];
    var n = 0;
    {
        var t: u8[>..] = "abc";
        fn put(h: H[>..]&) { h.push(H { s: t[..] }); }
        var mine: H[>..] = [];
        put(mine);
        n = mine.len;
        put(hs);
    }
    print(hs[0].s);
    n
};
fn main() { print(g); }
