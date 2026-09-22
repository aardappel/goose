// error: cannot clear related while x
// error: is still used: it may refer into related
// Iterating through r walks the slice r refers to, which s's rebind made a
// view of related (§5.2).
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var related: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    var s = entries[0].text;
    let r: u8[:]& = s;
    s = related[..];
    for &x in r {
        related.clear();
        print(x);
    }
}
