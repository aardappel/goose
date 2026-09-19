// error: storing a reference rooted at <temporary>, which does not outlive the destination
// A slice of an array literal is a view of a temporary, which no slot
// outlives (§9.2).
struct Keep { s: const i64[:] }
var kept = default<Keep>();
fn fill() { kept.s = [7, 7, 7][..]; }
fn main() {
    fill();
    print(kept.s[0]);
}
