// error: storing a reference rooted at v, which does not outlive the destination
// A variant constant is no storage: print renders it from a temporary,
// which a format hook taking it by reference is given (§3.7), and which a
// reference the hook keeps outlives.
enum Color { Red, Green }
var keep: (const Color&)? = null;
fn format(out: u8[>..]&, v: const Color&) { keep .= v; out.append("k"); }
fn main() {
    print(Color.Green);
    print(keep != null);
}
