// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// A view read out of a field never points into a grow-shrink array (§5.2),
// related included, though related is declared before the read: s, bound
// to one, points into none, and may not start to.
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var related: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    var s = entries[0].text;
    s = related[..];
    print(s);
}
