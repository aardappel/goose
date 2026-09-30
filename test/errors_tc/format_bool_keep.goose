// error: storing a reference rooted at v, which does not outlive the destination
// A comparison's result is no storage: print renders it from a temporary,
// which a format hook taking it by reference is given (§3.7), and which a
// reference the hook keeps outlives.
var g1: i64 = 5;
var keep: (const bool&)? = null;
fn format(out: u8[>..]&, v: const bool&) { keep .= v; out.append("k"); }
fn main() {
    print(g1 < 7);
    if keep != null {
        let y: bool = keep;
        if y { print("y"); }
    }
}
