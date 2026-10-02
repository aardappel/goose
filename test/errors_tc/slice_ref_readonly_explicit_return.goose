// error: storing a read-only slice of type u8[:] (read-only in this instantiation) in a slot of type u8[:]
// What a recursive callee returns of the slice a reference to a slice
// variable reaches is read-only where the variable holds a read-only slice,
// the reference written `&s` or not (§9.5).
recursive fn get(p: u8[:]&, n: i64) -> u8[:] {
    if n > 0 { return get(p, n - 1); }
    return p[..];
}
fn text(s: u8[:]) {
    var y: u8[:] = get(&s, 1);
    y[0] = 65;
}
fn main() { text("abc"); }
