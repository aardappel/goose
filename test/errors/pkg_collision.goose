// col.x is both the file col/x.goose beside this root and the declared
// package col's module: the root meant the package, so the file is an error.
// error: is both a file of this package
import col from "../packages/pkgs/col";
import col.x;
fn main() {}
