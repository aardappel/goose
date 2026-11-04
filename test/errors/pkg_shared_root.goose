// Two packages declared from one directory: a package is its directory.
// error: the packages col and x have the same root
import col from "../packages/pkgs/col";
import x from "../packages/pkgs/col";
fn main() {}
