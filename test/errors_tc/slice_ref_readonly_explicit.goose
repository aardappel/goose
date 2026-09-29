// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// `&s` of a slice variable holding a read-only slice is read-only, as binding
// `s` by reference is (§9.5), whatever the variable's type says: a recursive
// fn's parameter has no view of the slice its slot holds, so the callee sees
// the slice only as writable as the reference.
recursive fn poke(p: u8[:]&, n: i64) {
    if n > 0 { poke(p, n - 1); } else { p[0] = 65; }
}
fn text(s: u8[:]) { poke(&s, 1); }
fn main() { text("abc"); }
