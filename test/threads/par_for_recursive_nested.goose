// goose-threads
// A nested recursive function with a par for of its own, whose body calls
// it again (docs/design/child_threads.md §4): each specialization's copy of
// the loop is the one loop of the text, so the recursive call reuses the
// specialization it is in (ForLoop::parorigin), and the inner loops run as
// the outer chunk that reaches them does.
fn main() {
    recursive fn f(n: i64) -> i64 {
        if n == 0 { return 1; }
        var t = 0;
        par for k in 2 { t += f(n - 1); }
        return t;
    }
    print(f(3));
    print(f(5));
}
