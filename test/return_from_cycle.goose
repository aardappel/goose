// A long-distance return through a recursion cycle whose intermediate
// function was checked before the `return ... from` statement.
fn g(x: i64, n: i64) -> i64 {
    walk(n);
    return x;
}
recursive fn walk(n: i64) {
    if n > 0 {
        mid(n);
        return;
    }
    return 42 from g;
}
recursive fn mid(n: i64) {
    walk(n - 1);
    print("mid after walk");
}
fn main() {
    print(g(1, 0));
    print(g(1, 1));
}
