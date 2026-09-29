// error: operator + cannot be applied to i64? and i64
// A global's narrowing is flow like a local's: clear() rebinds opt to null
// before the break out of the block (§3.8, §6.4).
var n = 41;
var opt: i64? = null;
fn clear() { opt .= null; }
fn run(c: bool) {
    opt .= n;
    guard opt else { return; }
    block {
        if c { clear(); break; }
    }
    print(opt + 1);
}
fn main() { run(false); run(true); }
