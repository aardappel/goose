// error: returning a reference rooted in loc, which dies with this function
// A literal's references are bounded by the one whose root dies first
// (§9.2): loc, which ties with the temporary's parameter class at f's
// outermost depth.
struct H2 { a: const u8[:], b: const u8[:] }
fn make() -> u8[] { return "tmp"; }
fn f(s: const u8[:]) -> H2 {
    var loc: u8[3] = [108, 111, 99];
    return H2 { a: s, b: loc[..] };
}
fn main() {
    print(f(make()[..]).b);
}
