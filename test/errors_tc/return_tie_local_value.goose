// error: returning a reference rooted in loc, which dies with this function
// A temporary of the calling statement outlives the call, and its parameter
// class takes the depth of f's outermost scope (§9.2), which loc shares. A
// branch value that may be either is bounded by loc, which dies first.
fn make() -> u8[] { return "tmp"; }
fn f(s: const u8[:]) -> const u8[:] {
    var loc: u8[3] = [108, 111, 99];
    return if s.len > 100 { s } else { loc[..] };
}
fn main() {
    print(f(make()[..]));
}
