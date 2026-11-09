// error: a reference to an own value borrows it, as its plain type: write Buf?&, not (own Buf?)&
// No reference names an own value's slot as own: the slot is exchanged
// with take() and replace(), where it lies.
struct Buf { n: i64 }
fn f(p: (own Buf?)&) {}
fn main() {}
