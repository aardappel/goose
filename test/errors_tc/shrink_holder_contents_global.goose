// error: cannot call g: it shrinks GA while p is still used, and p may refer into GA
// p's words view names, whose element views GA, which g clears (§5.1).
struct P { words: (const u8[:])[:] }
var GA: u8[>..] = [];
fn g(p: P) {
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(p.words[0]);
}
fn main() {
    GA.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(GA[..]);
    g(P { words: names[..] });
}
