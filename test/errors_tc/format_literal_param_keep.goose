// error: storing a reference rooted at v, which does not outlive the destination
// A literal parameter is no storage but a constant of unknown value (§7.7):
// print renders it from a temporary, which a format hook taking it by
// reference is given (§3.7), and which a reference the hook keeps outlives
// once the function printing it returns.
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) { keep .= v; out.append("k"); }
fn show<T>(v: T) { print(v); }
fn main() {
    show(5);
    if keep != null {
        let y: i64 = keep;
        print(y == 5);
    }
}
