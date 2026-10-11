// A program with C of its own: the sidecar pkg_own_c.build names the C file
// and a define (docs/design/packages.md §4.7, §5.1). goose build compiles
// and links it; goose run builds it into a library the in-process run
// loads, and the extern fns reach it either way.
import std;

extern fn own_c_sum(a: i32, b: i32) -> i32;
extern fn own_c_count(s: const u8[:], c: u8) -> i64;

fn main() {
    print(own_c_sum(2, 3));
    print(own_c_count("banana", 'a'));
}
