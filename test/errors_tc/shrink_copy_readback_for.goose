// error: cannot clear out while w is still used
// w is read out of ws on every iteration, and the loop stores a view of out
// there after the shrink, which the next one reads (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    var ws: (const u8[:])[>..] = [];
    ws.push(words[0]);
    ws.push(words[1]);
    for w in ws {
        out.clear();
        out.append("ZZZZZZZZ");
        print(w);
        ws[1] = out[..];
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var words: (const u8[:])[>..] = [];
    words.push("x");
    words.push("y");
    emit(buf, words[..]);
}
