// error: cannot clear related while v
// error: is still used: it may refer into related
// s is read out of a field and r refers to s, but by the time r is loaded s
// views related (§5.2): what a reference to a slice variable leads to is the
// variable's slice now, not a slot read.
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
    let v = r;
    related.clear();
    print(v);
}
