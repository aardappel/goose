// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// s, bound to a view read out of a field, points into no grow-shrink array
// (§5.2), so it may not be rebound into related: the bytes of the slice r
// refers to would then be related's, which the clear frees.
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
