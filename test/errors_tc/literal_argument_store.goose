// error: storing a reference rooted at <temporary>, which does not outlive the destination
// An array literal passed to a slice parameter is a temporary, and so is
// the result of a call that returns its argument (§9.2).
fn id(xs: const i64[:]) -> const i64[:] { xs }
struct Keep { s: const i64[:] }
var kept = default<Keep>();
fn fill() { kept.s = id([7, 7, 7]); }
fn main() {
    fill();
    print(kept.s[0]);
}
