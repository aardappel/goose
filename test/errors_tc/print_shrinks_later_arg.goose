// error: cannot call f, which shrinks G while s (bound at
// print evaluates each argument as its turn comes, so an argument's own
// call runs before the arguments after it: s is read after f cleared G
// (§3.7, §5.2).
var G: i64[>..<] = [1, 2, 3];
fn f() -> i64 {
    G.clear();
    G.push(9);
    return 0;
}
fn main() {
    let s = G[..];
    print(f(), s);
}
