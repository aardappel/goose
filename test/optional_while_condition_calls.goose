// A call in a while condition rebinds the optional narrowed before the loop;
// the body tests it again before reading it.
var n = 41;
var cur: i64? = null;
var left = 3;
fn advance() -> bool {
    if left == 0 { cur .= null; return false; }
    left--;
    cur .= n;
    true
}
fn main() {
    cur .= n;
    print(cur + 1);
    var i = 0;
    while advance() { if cur { i += cur; } }
    print(i);
}
