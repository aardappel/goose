// error: storing a reference rooted at l, which does not outlive the destination
// A global slice variable assigned a view of a local array (§3.10, §9.2).
var GSV: const u8[:] = "x";
fn set() {
    var l: u8[..4] = "l";
    GSV = l;
}
fn main() {
    set();
    print(GSV);
}
