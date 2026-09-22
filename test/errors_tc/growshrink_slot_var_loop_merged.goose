// error: cannot clear related while s
// error: is still used: it may refer into related
// s is a view read out of a field when the loop starts, but the next
// iteration sees what the rebind at the end of the body left: a value whose
// root is y, as deep as s's, and which may be a view of related (§5.2).
struct Entry { text: u8[:] }
var related: u8[>..<] = [];
fn main() {
    related.push(9);
    var chars: u8[>..] = [];
    var y: u8[2] = [3, 4];
    var other: u8[>..<] = [];
    var entries: Entry[>..] = [];
    chars.append("ab");
    entries.push(Entry { text: chars[..] });
    var s = entries[0].text;
    let c = y[0] > 0;
    for i in 2 {
        related.clear();
        print(s);
        related.push(5);
        s = if c { related[..] } else { y[..] };
    }
}
