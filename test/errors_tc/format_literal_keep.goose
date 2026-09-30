// error: storing a reference rooted at v, which does not outlive the destination
// A literal is no storage: str renders it from a temporary, which a format
// hook taking it by reference is given (§3.7), and which a reference the
// hook keeps outlives.
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn main() {
    let s = str(5);
    if keep != null {
        let y: i64 = keep;
        print(s, " ", y == 5);
    }
}
