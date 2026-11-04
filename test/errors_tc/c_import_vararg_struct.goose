// error: argument 2 of cvs_log, of type P, cannot be passed to its ...: C reads a further argument by a scalar or pointer type
// A variadic C function reads its further arguments by the types its
// format names: scalars, pointers, bytes as a C string (spec §7.10).
import c "c_import_vararg_struct.h";
struct P { x: i32, y: i32 }
fn main() { cvs_log("%d", P { x: 1, y: 2 }); }
