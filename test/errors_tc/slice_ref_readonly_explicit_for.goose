// error: cannot write through this reference: non-writable provenance
// The elements a `for` walks through a reference to a slice are those of the
// slice in the slot it names, as writable as that is (§6.5, §9.5), here
// behind a recursive fn's parameter, which has no view of the slice.
recursive fn zap(p: u8[:]&, n: i64) {
    if n > 0 { zap(p, n - 1); } else { for &e in p { e = 65; } }
}
fn text(s: u8[:]) { zap(&s, 1); }
fn main() { text("abc"); }
