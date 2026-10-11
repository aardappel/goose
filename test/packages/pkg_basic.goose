// A program declaring packages from path sources (docs/design/packages.md
// §4): greet, whose entry requires tools, which the root declares as well;
// a module of greet imported by name; and inside greet an import relative
// to the importing file and one relative to the package's root.
import std;
import greet from "./pkgs/greet";
import tools from "./pkgs/tools";
import greet.extra;

fn main() {
    print(greet::hello("world"));
    print(greet::extra_code());
    print(tools::twice(4));
}
