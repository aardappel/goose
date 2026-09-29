// error: re-binding s to a reference that may point into a grow-shrink array (related), where it pointed into none
// s is read out of a field and r refers to s: a view out of a field points
// into no grow-shrink array (§5.2), so s may not be rebound into related,
// after which loading r would give a view of related, not a slot read.
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
