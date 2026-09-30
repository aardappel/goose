// error: storing a reference rooted at v, which does not outlive the destination
// An operator's result is no storage: print renders it from a temporary,
// which a format hook taking it by reference is given (§3.7), and which a
// reference the hook keeps outlives once the printing function returns.
var g1: i64 = 5;
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn helper(x: i64) { print(g1 + x); }
fn main() {
    helper(1000);
    if keep != null {
        let y: i64 = keep;
        print(y == 1005);
    }
}
