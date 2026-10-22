// A long-distance return out of a recursion a block starts (§7.9), where
// the function the block is handed to is the return's target: the block's
// body runs in search's own frame, which needs nothing of search itself,
// so twice may call search again however visit orders its recursive call
// and its return.
fn search<F>() -> i64 {
    F(0);
    return -1;
}
recursive fn visit(n: i64) -> i64 {
    if n < 10 { visit(n + 1); }
    if n == 5 { return n from search; }
    return 0;
}
fn twice<F>() -> i64 {
    let a = search(F);
    let b = search(F);
    return a + b;
}
fn main() {
    print(twice() { x => visit(x) });
}
