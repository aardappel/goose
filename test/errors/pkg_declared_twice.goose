// The same package declared twice by the root.
// error: is declared twice
import tools from "../packages/pkgs/tools";
import tools from "./pkg_other_tools";
fn main() {}
