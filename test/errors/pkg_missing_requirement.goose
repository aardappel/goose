// A requirement the root does not satisfy: greet needs tools, which this
// root does not declare, and the error says what line to add.
// error: greet needs the package tools
import greet from "../packages/pkgs/greet";
fn main() {}
