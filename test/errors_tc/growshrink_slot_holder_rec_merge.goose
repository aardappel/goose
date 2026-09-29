// error: storing a reference that may point into a grow-shrink array, as <recursive result> may
// m is a holder read out of an element of frags, which may be stored
// (§5.2), or a back edge's holder result, which may only be passed down
// (§7.8): what m holds may be either, so it is not stored.
struct Fragment { text: const u8[:] }
var frags: Fragment[>..<] = [];
recursive fn pass(h: Fragment, n: i64, c: bool) -> Fragment {
    if n == 0 { return h; }
    let local: u8[3] = [97, 98, 99];
    let r = pass(Fragment { text: local[..] }, n - 1, c);
    let m = if c { frags[0] } else { r };
    var keep: Fragment[>..] = [];
    keep.push(m);
    return h;
}
fn main() {
    frags.push(Fragment { text: "y" });
    print(pass(Fragment { text: "x" }, 2, true).text);
}
