// error: cannot write through this reference: non-writable provenance
// An `if` with no destination type gives a copy of the branch taken (§4.1),
// also where only one branch gives a value: a format hook taking the slice by
// reference gets a read-only reference to that temporary (§3.7, §9.5), where
// a reference parameter binds the branch itself.
var g: u8[>..] = [];
var g2: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = g[1..]; }
fn pick(n: i64) -> bool { return n > 2; }
fn main() {
    g.append("xyz");
    g2.append("abcd");
    var s = g2[..];
    print(if pick(3) { s } else { return; });
    print(s.len);
}
