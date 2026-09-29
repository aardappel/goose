// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// A slice variable bound by reference is no more writable than `&s` of it,
// its binding included (§9.5): given a literal, s holds a read-only slice,
// which a recursive fn's parameter, having no view of it, sees only as
// writable as the reference.
recursive fn poke(p: u8[:]&, n: i64) {
    if n > 0 { poke(p, n - 1); } else { p[0] = 65; }
}
fn text(s: u8[:]) { poke(s, 1); }
fn main() { text("abc"); }
