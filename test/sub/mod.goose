// Imported as `sub.mod` (relative to the main file); itself imports a sibling
// with the `.name` form (relative to this file).
import .sib;

fn from_mod(x: i64) -> i64 { sib_helper(x) + 1 }
