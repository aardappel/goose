// error: cannot call drop_last, which shrinks G: the for loop at
// error: iterates G[1..], which may refer into G
// A callee's shrink meets the loop's sequence at the call (§5.2, §6.5).
var G: i64[>..<] = [1, 2, 3, 4];
fn drop_last() { G.pop(); }
fn main() {
    for x in G[1..] {
        drop_last();
        G.push(9);
        print(x);
    }
}
