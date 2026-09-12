// Long-distance returns through recursion cycles that another function
// reaches with a cached call before the `return ... from` is checked.

// other reuses mid, which reaches g's target through its back edge into walk.
fn g(n: i64) -> i64 {
    walk(n);
    print("g normal exit");
    return 1;
}
recursive fn walk(n: i64) {
    if n < 0 { return; }
    if n > 1 {
        mid(n);
        return;
    }
    if n == 1 {
        other(n);
        print("walk after other");
        return;
    }
    return 42 from g;
}
recursive fn mid(n: i64) {
    walk(n - 1);
    print("mid after walk");
}
recursive fn other(n: i64) {
    mid(n);
    print("other after mid");
}

// Recursive descent: unary reuses factor before fail's return is checked.
fn parse(n: i64) -> i64 {
    expr(n);
    return 0;
}
recursive fn expr(n: i64) {
    if n == 3 { return; }
    if n >= 0 {
        term(n);
        unary(n);
        return;
    }
    fail();
}
recursive fn term(n: i64) { if n > 5 { factor(n); } }
recursive fn factor(n: i64) { expr(n - 1); }
recursive fn unary(n: i64) { factor(n); }
fn fail() { return -1 from parse; }

fn main() {
    print(g(2));
    print(g(-1));
    print(parse(2));
    print(parse(7));
}
