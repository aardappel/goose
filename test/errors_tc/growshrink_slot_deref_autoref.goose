// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// r was bound to s while s held a view read out of a field, which points
// into no grow-shrink array (§5.2), so s may not be rebound into related: r,
// sliced later, would lead to it, and the clear would free the slice.
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
