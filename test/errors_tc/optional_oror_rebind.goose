// The right operand of || runs after the left test narrowed its false side: a
// rebind there un-narrows what follows when the condition is false.
var n = 41;
var opt: i64? = null;
fn clearf() -> bool { opt .= null; return false; }
fn main() {
    opt .= n;
    if opt == null || clearf() { return; }
    print(opt + 1);
}
