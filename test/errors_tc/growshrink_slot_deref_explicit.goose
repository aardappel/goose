// error: cannot clear related while w
// error: is still used: it may refer into related
// A slice loaded through &s is what s holds when it is loaded, a view of
// related after the rebind (§5.2), not the view read out of a field that s
// may have been bound to first. That binding may be a view of other as well,
// which lets the rebind point s into a grow-shrink array.
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
    let r = &s;
    s = related[..];
    let w = r[0..1];
    related.clear();
    print(w);
}
