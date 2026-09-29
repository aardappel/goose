// error: cannot clear g2: global keep may hold a reference into it
// stash keeps a slice of what its parameter's slot holds, which this call
// passes a view of g2 in: keep holds a view of g2 when g2 shrinks (§5.1).
var g2: u8[>..] = [];
var keep: (u8[:])[>..] = [];
fn stash(p: u8[:]&) { keep.push(p[0..1]); }
fn main() {
    g2.append("abc");
    var s = g2[..];
    stash(s);
    g2.clear();
    print(keep[0]);
}
