// error: storing a reference rooted at <temporary>, which does not outlive the destination
// r would name the field of mk's result, a temporary that lasts until the
// end of the statement (§3.8, §9.2), whatever its slice views; a reference
// into a temporary is read-only (§9.5), so r is a const one.
struct H { s: u8[:] }
var g: u8[>..] = [];
fn mk() -> H { return H { s: g[0..2] }; }
fn main() {
    g.append("abc");
    let r: const (u8[:])& = mk().s;
    print(r);
}
