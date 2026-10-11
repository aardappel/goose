// A program using a package whose native part is a CMake project (cmk):
// skipped by the runner where no cmake is on PATH.
import std;
import cmk from "./pkgs/cmk";

fn main() {
    print(cmk::triple(4));
    print(cmk::triple(-5));
}
