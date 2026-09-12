// A guard narrows the code after it by its condition, except what the right
// operand of its && rebinds.
var n = 41;
var opt: i64? = null;
fn clear() -> bool { opt .= null; return true; }
fn main() {
    opt .= n;
    guard opt != null && clear() else { return; }
    print(opt + 1);
}
