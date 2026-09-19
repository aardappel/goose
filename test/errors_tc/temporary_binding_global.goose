// error: binding g to a slice rooted at a temporary, which does not outlive it
// A global initializer's temporaries end with it too.
fn mk() -> u8[] { "hello" }
fn id(xs: const u8[:]) -> const u8[:] { xs }
let g = id(mk());
fn main() { print(g); }
