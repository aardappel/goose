// error: storing a reference rooted at v, which does not outlive the destination
// default<i64>() is a value built in a temporary, as a literal is (§9.2):
// print renders it from there (§3.7), which a format overload taking it by
// reference cannot keep past the call.
var keep: const i64&? = null;
fn format(out: u8[>..]&, v: i64&) { keep .= v; out.append("x"); }
fn show() { print(default<i64>()); }
fn main() {
    show();
    if keep { print(keep as i32); }
}
