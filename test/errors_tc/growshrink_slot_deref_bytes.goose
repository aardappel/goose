// error: cannot clear related while b
// error: is still used: it may refer into related
// The bytes of the slice r refers to are those of related once s is rebound
// (§5.2).
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
    let b = r.bytes_of();
    related.clear();
    print(b);
}
