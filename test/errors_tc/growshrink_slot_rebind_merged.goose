// error: cannot clear related while s
// error: is still used: it may refer into related
// s was bound to a view read out of a field, then rebound to a value that
// may be a view of related (§5.2): what s holds is no longer only what a
// field held. The read-back rule already let s point into other, so the
// rebind may add related.
struct Entry { text: u8[:] }
fn main() {
    var related: u8[>..<] = [];
    related.push(99);
    {
        var chars: u8[>..] = [];
        var y: u8[2] = [1, 2];
        var other: u8[>..<] = [];
        var entries: Entry[>..] = [];
        chars.append("ab");
        entries.push(Entry { text: chars[..] });
        var s = entries[0].text;
        let c = y[0] > 0;
        s = if c { related[..] } else { y[..] };
        related.clear();
        print(s);
    }
}
