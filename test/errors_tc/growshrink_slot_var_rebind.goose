// error: cannot clear related while s
// error: is still used: it may refer into related
// s starts out as a view read out of a field, which never points into a
// grow-shrink array (§5.2), or a view of other, which lets the rebind at the
// end of the loop body point it into related. Neither points into related,
// but s is a var rooted at related's depth, so a rebind at that depth may
// have retargeted it there (§9.2): this one does, and the next iteration's
// clear frees related before s is printed.
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var related: u8[>..<] = [];
    var other: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    related.push(99);
    other.push(98);
    entries.push(Entry { text: chars[..] });
    var s = if other.len > 5 { other[..] } else { entries[0].text };
    for i in 2 {
        related.clear();
        print(s);
        related.push(100);
        s = related[..];
    }
}
