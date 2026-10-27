// error: cannot clear related while s
// error: is still used: it may refer into related
// s is bound to a view read out of a field, which never points into a
// grow-shrink array (§5.2), but s is a var whose root, entries, is as deep
// as related: a same-depth rebind could retarget it there (§5.1, §9.2), as
// the one at the end of the loop body does, so the clear is checked against
// s. That alone decides it: without the rebind the clear is rejected all
// the same.
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var related: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    var s = entries[0].text;
    for i in 2 {
        related.clear();
        print(s);
        related.push(100);
        s = related[..];
    }
}
