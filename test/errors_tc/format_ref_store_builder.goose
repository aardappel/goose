// error: storing a reference rooted at out, which does not outlive the destination
// A format overload taking a value by reference writes what a reference
// argument names (§3.7), so what it stores there must outlive that storage
// (§9.2): a view of the text str builds, a temporary, may not be kept in
// the struct the reference names.
struct H { s: u8[:] }
var keep: u8[>..] = [];
fn format(out: u8[>..]&, v: H&) { out.append("H"); v.s = out[..]; }
fn main() {
    var h = H { s: keep[..] };
    let r .= h;
    let t = str(r);
    print(h.s.len, " ", t);
}
