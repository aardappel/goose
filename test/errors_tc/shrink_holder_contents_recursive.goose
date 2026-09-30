// error: cannot call walk: it shrinks a while h is still used, and h may refer into a
// Every level keeps v in a holder of its own and reads it after the
// deepest level clears a: v views tt, whose element views a (§5.1).
struct P { words: (const u8[:])[:] }
recursive fn walk(a: u8[>..]&, v: (const u8[:])[:], n: i64) {
    let h = P { words: v };
    if n == 0 {
        a.clear();
        a.append("ZZZZZZZZ");
        return;
    }
    walk(a, v, n - 1);
    print(h.words[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var tt: (const u8[:])[>..] = [];
    tt.push(a[..]);
    walk(a, tt[..], 2);
}
