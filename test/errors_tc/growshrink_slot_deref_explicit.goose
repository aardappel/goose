// error: cannot clear related while w
// error: is still used: it may refer into related
// A slice loaded through &s is what s holds when it is loaded, a view of
// related after the rebind (§5.2), not the view read out of a field that s
// was bound to first.
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var related: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    var s = entries[0].text;
    let r = &s;
    s = related[..];
    let w = r[0..1];
    related.clear();
    print(w);
}
