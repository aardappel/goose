// error: returning a reference rooted in loc, which dies with this function
// A holder variable's contents are bounded by what was stored into it that
// dies first (§9.2): loc, which ties with the temporary's parameter class at
// f's outermost depth.
struct H { s: const u8[:] }
fn make() -> u8[] { return "tmp"; }
fn f(s: const u8[:]) -> H {
    var loc: u8[3] = [108, 111, 99];
    var h = H { s: s };
    if s.len < 100 { h.s = loc[..]; }
    return h;
}
fn main() {
    print(f(make()[..]).s);
}
