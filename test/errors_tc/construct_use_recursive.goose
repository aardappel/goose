// error: cannot call depth in the value assigned to G: depth, still being checked, may use G
// A call back into a recursive cycle still being checked is not known in
// full, so it counts as using every global.
var G: i64[>..<] = [1, 2, 3];
recursive fn depth(n: i64) -> i64 {
    if n == 0 { return G.len; }
    return rebuild(n - 1);
}
recursive fn rebuild(n: i64) -> i64 {
    G = [depth(n), 5];
    return 0;
}
fn main() {
    print(depth(3));
    print(G);
}
