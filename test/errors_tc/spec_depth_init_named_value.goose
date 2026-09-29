// error: storing a reference rooted at t, which does not outlive the destination
// So do the functions it is handed to as a value: both's first call of each
// passes its own mine, in which put may store a view of t, and its second the
// initializer's hs, which outlives t.
struct H { s: const u8[:] }
fn each<F>(h: H[>..]&) { F(h); }
fn both<F>(h: H[>..]&) {
    var mine: H[>..] = [];
    each(mine, F);
    each(h, F);
}
let g = block {
    var hs: H[>..] = [];
    {
        var t: u8[>..] = "abc";
        fn put(h: H[>..]&) { h.push(H { s: t[..] }); }
        both(hs, put);
    }
    print(hs[0].s);
    hs.len
};
fn main() { print(g); }
