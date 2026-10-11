// A program using a package with a native part (cmath2, C under its
// sidecar) beside pure Goose packages: goose build links cmath2's objects,
// goose run loads its library.
import std;
import cmath2 from "./pkgs/cmath2";
import greet from "./pkgs/greet";
import tools from "./pkgs/tools";

fn main() {
    print(cmath2::square(7));
    let a: i32[3] = [1, 2, 3];
    let b: i32[3] = [4, 5, 6];
    print(cmath2::dot(a, b));
    print(greet::hello("packages"));
}
