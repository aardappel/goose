// error: cannot clear src while v is still used: it may hold a reference or slice into it
// load's parameter names a field of an element of entries, whose slice the
// callee only knows to outlive the caller's storage (§5.1, §9.5): what it
// loads and returns may view src.
struct E { text: u8[:] }
fn load(p: u8[:]&) -> u8[:] { return p; }
fn main() {
    var src: u8[>..] = [];
    src.append("abcdef");
    var entries: E[>..] = [];
    entries.push(E { text: src[0..2] });
    let r = &entries[0].text;
    let v = load(r);
    src.clear();
    src.append("QQQQQQ");
    print(v);
}
