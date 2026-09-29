// error: storing a reference rooted at s, which does not outlive the destination
// `.=` binds s itself (§3.8), which dies at the end of its block, while o
// lives on.
var g: u8[>..] = [];
fn main() {
    g.append("abc");
    var o: u8[:]&? = null;
    {
        var s = g[0..1];
        o .= s;
    }
    if o { print(o); }
}
