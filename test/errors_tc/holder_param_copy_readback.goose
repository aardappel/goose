// error: returning a reference rooted in y, which dies with this function
// A local copy of a by-value parameter holds what the caller's value held, so
// a slice read back out of the copy is bounded by the parameter's holder
// class as well (§9.5).
struct W { s: const u8[:] }
fn get(w: W) -> const u8[:] { let w2 = w; w2.s }
fn mk() -> const u8[:] {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    let w = W { s: y[..] };
    get(w)
}
fn main() { let s = mk(); print(s); }
