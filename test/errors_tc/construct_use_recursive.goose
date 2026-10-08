// error: cannot call depth in the value assigned to G: depth, still being checked, may use G
// A call back into a recursive cycle still being checked is not known in
// full, so it counts as using every global. (The call is in the condition,
// not inside the literal: a literal under construction may not call into
// the cycle either, §7.8.)
var G: i64[>..<] = [1, 2, 3];
recursive fn depth(n: i64) -> i64 {
    if n == 0 { return G.len; }
    return rebuild(n - 1);
}
recursive fn rebuild(n: i64) -> i64 {
    G = if depth(n) > 0 { [4, 5] } else { [6] };
    return 0;
}
fn main() {
    print(depth(3));
    print(G);
}
