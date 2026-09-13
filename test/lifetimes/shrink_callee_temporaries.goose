// A callee checked on its first call does not inherit the caller's pending
// temporaries: its own fresh local may shrink.
struct P { s: const u8[:], n: i64 }
fn show(p: P) { print(p.s, " ", p.n); }
fn scratch() -> i64 {
    var buf: u8[>..<] = [1, 2];
    buf.pop();
    return buf.len;
}
fn main() {
    var words: const u8[:][>..<] = ["hi"];
    show(P { words.pop(), scratch() });
}
