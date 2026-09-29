// error: storing a reference rooted at p, which does not outlive the destination
// stash stores a slice of what its parameter's slot holds into a global:
// fine where that is a global's view, but t's is of loc, which keep would
// outlive (§9.2). The call is checked for the depth its slot's slice has.
var g2: u8[>..] = [];
var keep: (u8[:])[>..] = [];
fn stash(p: u8[:]&) { keep.push(p[0..1]); }
fn main() {
    g2.append("abc");
    var loc: u8[>..] = [];
    loc.append("xyz");
    {
        var s = g2[..];
        stash(s);
        var t = loc[..];
        stash(t);
    }
    print(keep.len);
}
