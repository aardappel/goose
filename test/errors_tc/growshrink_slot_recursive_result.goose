// error: cannot clear related while v
// error: is still used: it may refer into related
// The back edge's result may come from a return not checked yet, here a
// view of related (§5.2, §7.8), though the one checked so far is read out
// of a field.
struct Entry { text: u8[:] }
var related: u8[>..<] = [];
var chars: u8[>..] = [];
var entries: Entry[>..] = [];
recursive fn pick(n: i64) -> u8[:] {
    if n == 0 { return entries[0].text; }
    let v = pick(n - 1);
    related.clear();
    print(v);
    related.push(1);
    return related[..];
}
fn main() {
    chars.append("ok");
    related.push(99);
    entries.push(Entry { text: chars[..] });
    print(pick(2));
}
