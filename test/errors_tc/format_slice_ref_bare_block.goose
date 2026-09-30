// error: cannot write through this reference: non-writable provenance
// A bare block's value is a copy of its final expression (§4.1), which the
// optimizer keeps where it folds the block into that expression: a format
// hook taking the slice by reference gets a read-only reference to that
// temporary (§3.7, §9.5).
var g: u8[>..] = [];
var g2: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn main() {
    g.append("xyz");
    g2.append("abcd");
    var s = g2[..];
    print({ s });
    print(s.len);
}
