// error: storing a reference rooted at s, which does not outlive the destination
// keep may store a global's slice in a global, not a local's: the
// specialization checked for gtext does not serve inner's call.
struct H { s: const u8[:] }
var gh: H[>..] = [];
let gtext: u8[] = "global";
fn keep(s: const u8[:]) { gh.push(H { s: s }); }
fn inner() {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    keep(y[..]);
}
fn main() {
    keep(gtext[..]);
    inner();
    print(gh[1].s);
}
