// error: storing a reference rooted at <temporary>, which does not outlive the destination
// A loop over an array literal by reference binds references into a
// temporary (§9.2).
struct Keep { r: const i64&? }
var kept = default<Keep>();
fn fill() { for &x in [1, 2, 3] { kept.r .= x; } }
fn main() {
    fill();
    if kept.r { print(kept.r); }
}
