// error: storing a reference rooted at text, which does not outlive the destination
// A reference read back out of a local holder may point at anything stored
// there whose storage can hold its pointee: here a u8 of the shorter-lived
// text the holder also views (§9.5).
struct P { input: const u8[:], items: (u8&)[>..] }
struct S { refs: (u8&)[>..] }
fn prep(s: S&, pool: u8[>..]&) {
    var text: u8[>..] = [];
    text.append("abc");
    var p = P { input: text, items: [] };
    p.items.push(pool[0]);
    s.refs.append(p.items);
}
fn main() {
    var pool: u8[>..] = [1];
    var s = S { refs: [] };
    prep(s, pool);
    print(s.refs.len);
}
