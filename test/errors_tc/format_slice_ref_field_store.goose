// error: cannot clear g while s is still used
// A slice rendered as a field of the value printed lies in that value (§3.7):
// a format hook taking it by reference re-points s.f, which holds a view of
// g once print returns (§5.1).
struct S { f: u8[:] }
var g: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn main() {
    g.append("xyz");
    var local: u8[>..] = [];
    local.append("abc");
    var s = S { f: local[..] };
    print(s);
    g.clear();
    print(s.f[0]);
}
