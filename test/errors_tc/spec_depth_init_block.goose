// error: storing a reference rooted at t, which does not outlive the destination
// And those a block written in a global initializer is handed to, as for a
// block written in a function (errors_tc/spec_depth_function_value): the
// block may store a view of t in both's mine, but not in hs, which outlives t.
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
        both(hs) { h => h.push(H { s: t[..] }); };
    }
    print(hs[0].s);
    hs.len
};
fn main() { print(g); }
