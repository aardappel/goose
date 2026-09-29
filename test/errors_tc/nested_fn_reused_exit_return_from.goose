// error: variable n may be used before it is assigned
// bail's check, in the branch that assigns n first, leaves setn with n
// assigned; the other branch's call reuses that check and leaves setn the
// same way with n unassigned (§4.4, §7.5, §7.9).
fn run(c: bool, x: bool) {
    var n: i64;
    fn setn(d: bool) {
        fn bail(e: bool) { if e { return from setn; } }
        if x { n = 1; bail(d); } else { bail(d); }
        n = 5;
    }
    setn(c);
    print(n);
}
fn main() { run(false, false); run(true, false); }
