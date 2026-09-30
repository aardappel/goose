// error: cannot write through this reference: non-writable provenance
// An `if` with no destination type gives a copy of the branch taken (§4.1):
// a format hook taking the slice by reference gets a read-only reference to
// that temporary (§3.7, §9.5), whichever variable the branch names.
var g: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn main() {
    g.append("xyz");
    var h: u8[>..] = [];
    h.append("abc");
    var s1 = h[..];
    var s2 = h[1..];
    var c = true;
    print(if c { s1 } else { s2 });
    print(s1.len);
}
