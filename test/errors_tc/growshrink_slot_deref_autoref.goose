// error: cannot clear related while w
// error: is still used: it may refer into related
// r was bound to s while s held a view read out of a field; s views related
// by the time r is sliced (§5.2).
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
    let w = r[0..1];
    related.clear();
    print(w);
}
