// error: returning a reference rooted in local, which dies with this function
// A back edge maps a holder result's roots through the arguments it passes
// (§7.8): the first return is the holder parameter, so the back edge's
// result holds what its argument held, a view of this activation's `local`,
// which may not be returned.
struct Fragment { text: const u8[:] }

recursive fn pass(h: Fragment, n: i64) -> Fragment {
    if n == 0 { return h; }
    let local: u8[3] = [97, 98, 99];
    let r = pass(Fragment { text: local[..] }, n - 1);
    return r;
}

fn main() {
    var word: u8[>..] = "x";
    print(pass(Fragment { text: word[..] }, 2).text);
}
