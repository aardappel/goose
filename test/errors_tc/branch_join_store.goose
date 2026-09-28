// error: storing a reference rooted at local, which does not outlive the destination
// The joined slice views the array itself (§6.4), which the global it is
// stored in outlives (§9.2).
var g: const u8[:] = "start";
fn set(c: bool) {
    var local: u8[..8] = "loc";
    g = if c { "x" } else { local };
}
fn main() {
    set(false);
    print(g);
}
