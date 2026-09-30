// error: cannot call emit: it shrinks buf while words is still used, and words may refer into buf
// A slice of a call's result: nothing records what the temporary's
// elements hold, and here one views buf (§5.1).
fn mk(a: u8[>..]&) -> (const u8[:])[] { [a[..], "q"] }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    emit(buf, mk(buf)[..]);
}
