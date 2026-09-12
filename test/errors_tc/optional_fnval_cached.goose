// A function value keeps the optionals of the environment it was written in
// as part of a cached body's assumptions.
fn apply<F>() { F(); }
fn main() {
    var n = 41;
    var opt: i64? = n;
    fn use() { print(opt + 1); }
    if opt { apply(use); }
    opt .= null;
    apply(use);
}
