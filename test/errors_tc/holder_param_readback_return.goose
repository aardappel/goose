// error: returning a reference rooted in y, which dies with this function
// A slice read out of a by-value parameter's field points where the caller's
// value pointed: it is bounded by the parameter's holder class, which maps
// back to mk's own y at the call, not rooted at static data (§9.5).
struct W { s: const u8[:] }
fn get(w: W) -> const u8[:] { w.s }
fn mk() -> const u8[:] {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    let w = W { s: y[..] };
    get(w)
}
fn main() { let s = mk(); print(s); }
