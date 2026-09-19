// error: returning a reference rooted in ls, which dies with this function
// The words the lines a parameter points at list are storage of the caller's,
// two references away, that first cannot enumerate: a word kept in a local
// array and read back out of it is bounded by the parameter's root, which
// maps back to mk's ls at the call (§9.5).
struct Line { words: const u8[:][:] }
fn first(lines: Line[:]) -> const u8[:] {
    var parts: const u8[:][>..] = [];
    parts.push(lines[0].words[0]);
    parts[0]
}
fn mk() -> const u8[:] {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    var ws: const u8[:][>..] = [];
    ws.push(y[..]);
    var ls: Line[>..] = [];
    ls.push(Line { words: ws[..] });
    first(ls)
}
fn main() { let s = mk(); print(s); }
