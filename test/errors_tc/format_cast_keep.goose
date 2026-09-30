// error: storing a reference rooted at v, which does not outlive the destination
// A cast's result is no storage: format renders it from a temporary, which
// a format hook taking it by reference is given (§3.7), and which a
// reference the hook keeps outlives.
var g1: i64 = 5;
var keep: (const i32&)? = null;
fn format(out: u8[>..]&, v: const i32&) { keep .= v; out.append("k"); }
fn main() {
    var b: u8[>..] = [];
    format(b, g1 as i32, ".");
    if keep != null {
        let y: i32 = keep;
        print(b, " ", y == 5);
    }
}
