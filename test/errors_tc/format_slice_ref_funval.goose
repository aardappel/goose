// error: cannot write through this reference: non-writable provenance
// A function value's call gives a copy of its body's value, a temporary as
// any call's result is (§9.2), even where the body names a variable: a
// format hook taking the slice by reference gets a read-only reference to
// that temporary (§3.7, §9.5).
var g: u8[>..] = [];
var g2: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn show<F>() { print(F()); }
fn main() {
    g.append("xyz");
    g2.append("abcd");
    var s = g2[..];
    show() { s };
    print(s.len);
}
