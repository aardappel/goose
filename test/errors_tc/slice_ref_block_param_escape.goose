// error: storing a reference rooted at s, which does not outlive the destination
// The block's parameter names run's slice variable s itself (§4.1, §7.6),
// which dies with run, while o lives on.
var g: u8[>..] = [];
fn run<F>() {
    var s = g[0..1];
    F(s);
}
fn main() {
    g.append("abc");
    var o: u8[:]&? = null;
    run() { p: u8[:]& => o .= p; };
    if o { print(o); }
}
