// error: storing a reference rooted at <temporary>, which does not outlive the destination
// The fill form of an array literal is a temporary like the element form
// (§9.2).
fn id(xs: const i64[:]) -> const i64[:] { xs }
struct Keep { s: const i64[:] }
var kept = default<Keep>();
fn fill() { kept.s = id([7; 3]); }
fn main() {
    fill();
    print(kept.s[0]);
}
