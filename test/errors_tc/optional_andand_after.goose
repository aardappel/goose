// A rebind in the right operand of && holds after the condition too.
var n = 41;
var g: i64? = null;
fn clear() -> bool { g .= null; return true; }
fn main() {
    g .= n;
    let c = n == 41;
    if c && clear() { print(1); }
    print(g + 1);
}
