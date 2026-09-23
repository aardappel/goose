// error: cannot clear related while v
// error: is still used: it may refer into related
// A call's result is read out of a field (§5.2) only where every return's
// is: pick's first return is, but its second is a view of related, rooted
// elsewhere, and the result may be either (§9.2).
struct Entry { text: u8[:] }
var related: u8[>..<] = [];
fn pick(e: Entry[>..]&, c: bool) -> u8[:] {
    if c { return e[0].text; }
    return related[..];
}
fn main() {
    var chars: u8[>..] = [];
    var entries: Entry[>..] = [];
    chars.append("ok");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    let v = pick(entries, related.len > 5);
    related.clear();
    print(v);
}
