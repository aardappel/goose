// The right operand of && runs after the left test narrowed: a rebind there
// un-narrows the guarded region.
var n = 41;
var opt: i64? = null;
fn clear() -> bool { opt .= null; return true; }
fn main() {
    opt .= n;
    if opt && clear() { print(opt + 1); }
}
