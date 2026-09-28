// error: variable GLATE may be used before it is assigned: globals initialize in declaration order, imported files first (§11.1)
// error: global_before_init_slice.goose:6 has run
// Globals initialize in declaration order (§11.1): GV's initializer slices
// GLATE before GLATE's own initializer has run.
var GV: const u8[:] = GLATE[..];
var GLATE: u8[..8] = "late";
fn main() { print(GV); }
