// error: cannot call walk: it may shrink G while s is still used, and s may refer into G
// A call back into the cycle taken to be balanced records no pair for the
// view of a parameter's class live across it (§5.1); when the assumption
// fails, the pair it would have recorded is recorded after all, and main,
// which passes G's view, may not make the two one array: the deepest walk
// clears G while every shallower one still reads s.
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
