// runtime-debug: a next_pow2 past i64's maximum positive power of two fails an assert
import std;

fn main() {
    let x: i64 = (1 << 62) + 1;
    print(next_pow2(x));
}
