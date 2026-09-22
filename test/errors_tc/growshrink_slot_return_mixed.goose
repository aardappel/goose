// error: cannot clear related while v
// error: is still used: it may refer into related
// Only one of pick's returns is read out of a field; the other is a view of
// related (§5.2), and the result may be either.
struct Entry { text: u8[:] }
var related: u8[>..<] = [];
var chars: u8[>..] = [];
var entries: Entry[>..] = [];
fn pick(c: bool) -> u8[:] {
    if c { return entries[0].text; }
    return related[..];
}
fn main() {
    chars.append("ok");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    let v = pick(related.len > 5);
    related.clear();
    print(v);
}
