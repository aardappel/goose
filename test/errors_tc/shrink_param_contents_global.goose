// error: cannot call emit, which shrinks GB: global GN may hold a reference into it
// The call leaves what a global holds to the judgement of the globals,
// which finds GN's view of GB (§5.1).
var GB: u8[>..] = [];
var GN: (u8[:])[>..] = [];
fn emit(out: u8[>..]&, words: (u8[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn main() {
    GB.append("abc");
    GN.push(GB[..]);
    emit(GB, GN[..]);
}
