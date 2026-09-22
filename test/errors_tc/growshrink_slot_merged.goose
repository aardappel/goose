// error: cannot clear related while v
// error: is still used: it may refer into related
// Only one branch of v is read out of a field; the other is a view of
// related (§5.2).
struct Entry { text: u8[:] }
fn main() {
    var chars: u8[>..] = [];
    var entries: Entry[>..] = [];
    var related: u8[>..<] = [];
    chars.append("ok");
    related.push(7);
    entries.push(Entry { text: chars[..] });
    let c = related.len > 0;
    let v = if c { entries[0].text } else { related[..] };
    related.clear();
    print(v);
}
