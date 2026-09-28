// error: storing a reference rooted at <temporary>, which does not outlive the destination
// A call's array result is a temporary of the declaration's statement, which
// the whole-array slice of it would outlive (§3.10, §9.2).
fn make() -> u8[..8] { "made" }
fn main() {
    let s: const u8[:] = make();
    print(s);
}
