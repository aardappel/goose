// error: storing a reference rooted at <temporary>, which does not outlive the destination
// A slice of a block's value views the block's temporary copy, which no
// slot outlives (§9.2).
struct Keep { s: const i64[:] }
var kept = default<Keep>();
var g: i64[3] = [1, 2, 3];
fn fill() { kept.s = (block { g })[..]; }
fn main() {
    fill();
    print(kept.s[0]);
}
