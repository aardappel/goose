// An empty range has no value to draw: rand_range asserts size > 0, as
// rand_int asserts n > 0.
import std;
fn main() {
    var r = rng { 1 };
    print(r.rand_range(5, 1));
    print(r.rand_range(5, 0));
}
