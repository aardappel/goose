// A shift wraps at its operands' type in every build (§6.2: the count is
// masked, and the result is truncated back to the width), so a constant one
// folds to the value the same shift computes at run time -- including where
// that leaves the type's range, which no shift aborts on.
import std;

let K: i8 = 65;

fn main() {
    // 65 << 2 is 260, which is 4 at i8, and is the array's length here.
    let a: i64[K << 2] = [7; K << 2];
    print(a.len);
    var v: i8 = 65;
    print(v << 2);
    // The count is masked to 0..width-1 first: << 8 at i8 is << 0.
    let b: i64[K << 8] = [1; K << 8];
    print(b.len);
    print(v << 8);
}
