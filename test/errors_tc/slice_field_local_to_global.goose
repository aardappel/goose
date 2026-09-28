// error: storing a reference rooted at l, which does not outlive the destination
// A global's slice field assigned a view of a local array (§3.10, §9.2).
struct H { s: const u8[:] }
var GH = H { s: "x" };
fn set() {
    var l: u8[..4] = "l";
    GH.s = l;
}
fn main() {
    set();
    print(GH.s);
}
