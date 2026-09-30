// error: storing a reference rooted at v, which does not outlive the destination
// A payload-less variant is no storage: print renders it from a temporary of
// the printing statement (§3.7), and each part of that value lies there too,
// so a format overload taking a struct among its parts by reference cannot
// keep it past the call (§9.2), whichever variant the value is: a
// variable-mode ADT's payload is rendered where it lies, not from a copy.
struct P { a: i64, b: i64 }
enum Msg { Quit, Say { p: P, text: u8[] } }
var keep: const P&? = null;
fn format(out: u8[>..]&, v: P&) { keep .= v; out.append("x"); }
fn show() { print(Msg.Quit); }
fn main() {
    show();
    if keep { print(keep.a); }
}
