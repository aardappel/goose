// error: cannot call walk: it shrinks G while s is still used, and s may refer into G
// A call back into the cycle applies the record the cycle's first round
// made (§7.8): the pair for the view of a parameter's class live across it
// reaches main, which passes G's view and may not make the two one array:
// the deepest walk clears G while every shallower one still reads s.
var G: i64[>..<] = [1, 2, 3];
recursive fn walk(s: i64[:], d: i64) -> i64 {
    var t = 0;
    if d < 3 { t = walk(s, d + 1); }
    t += s[0];
    if d == 3 {
        G.clear();
        G.push(77);
    }
    return t;
}
fn main() {
    print(walk(G[..], 1));
}
