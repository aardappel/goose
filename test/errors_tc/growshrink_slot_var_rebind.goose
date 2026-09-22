// error: cannot clear related while s
// error: is still used: it may refer into related
// s starts out as a view read out of a field, which never points into a
// grow-shrink array (§5.2), but it is a var bound at related's depth: the
// rebind further down the loop puts a view of related there, which the next
// iteration's clear frees before s is printed.
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
