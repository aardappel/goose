// runtime-debug: a negative number has no integer square root; isqrt aborts.
import std;
fn main() {
    print(isqrt(0));
    print(isqrt(-1));
}
