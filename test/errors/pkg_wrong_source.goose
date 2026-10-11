// The root declares tools from another place than greet requires it from.
// error: declared from a different source
import greet from "../packages/pkgs/greet";
import tools from "./pkg_other_tools";
fn main() {}
