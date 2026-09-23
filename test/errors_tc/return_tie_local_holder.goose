// error: returning a reference rooted in loc, which dies with this function
// Holder values from the two branches: the one holding a view of loc, which
// ties with the temporary's parameter class at f's outermost depth and dies
// first, bounds the merged value (§9.2).
struct H { s: const u8[:] }
fn make() -> u8[] { return "tmp"; }
fn f(s: const u8[:]) -> H {
    var loc: u8[3] = [108, 111, 99];
    return if s.len > 100 { H { s: s } } else { H { s: loc[..] } };
}
fn main() {
    print(f(make()[..]).s);
}
