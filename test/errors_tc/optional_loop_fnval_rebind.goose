// error: optional opt is rebound later in this loop, so its narrowing does not hold on the next iteration
// A loop body that rebinds a narrowed optional through a function value
// breaks the narrowing its next iteration relies on.
fn apply<F>() { F(); }
fn main() {
    var n = 41;
    var opt: i64? = n;
    fn clear() { opt .= null; }
    assert(opt);
    for i in 2 {
        print(opt + 1);
        apply(clear);
    }
}
