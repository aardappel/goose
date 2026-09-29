// error: cannot clear related while x
// error: is still used: it may refer into related
// r, bound to s by reference, refers to s itself as `&s` does (§3.8, §4.1):
// a `for` through it walks what s holds when it starts, related after the
// rebind (§5.2), not the view read out of a field that s may have been bound
// to first. That binding may be a view of other as well, which lets the
// rebind point s into a grow-shrink array.
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
    let r: u8[:]& = s;
    s = related[..];
    for &x in r {
        related.clear();
        print(x);
    }
}
