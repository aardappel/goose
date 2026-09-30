// error: storing a slice rooted at g into s through show's parameter r
// A reference argument renders its pointee where it lies (§3.7): a format
// hook taking the slice by reference re-points the variable the reference
// names, so show stores a view of g into s, bound to one of local (§9.2).
var g: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn show(r: u8[:]&) { print(r); }
fn main() {
    g.append("xyz");
    var local: u8[>..] = [];
    local.append("abc");
    var s = local[..];
    show(s);
    g.clear();
    print(s[0]);
}
