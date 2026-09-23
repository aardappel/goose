// error: returning a reference rooted in loc, which dies with this function
// A same-depth rebind leaves the variable bounded by the root that dies
// first (§9.2): loc, which ties with the temporary's parameter class at f's
// outermost depth, whichever binding came first.
fn make() -> u8[] { return "tmp"; }
fn f(s: const u8[:]) -> const u8& {
    var loc: u8[3] = [108, 111, 99];
    var r: const u8& = loc[0];
    if s.len > 100 { r .= s[0]; }
    return r;
}
fn main() {
    print(f(make()[..]));
}
