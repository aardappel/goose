import std;

// Compile-time extents use the same widths and signedness as runtime arithmetic.
let MAX: u8 = 255;
let ONE: u8 = 1;
let WORD: u64 = 18446744073709551615;
struct Data { value: i64[WORD >> 63] }

fn extents() {
    let high: i64[18446744073709551615 >> 63] = [7];
    let quotient: i64[18446744073709551615 / 9223372036854775808] = [8];
    let wrapped: i64[MAX + 1] = [];
    let shifted: i64[ONE << 8] = [9];
    let inverted: i64[~MAX] = [];
    let remainder: i64[-9223372036854775808 % -1] = [];
    let data = Data { [10] };
    print(high, quotient, wrapped.len, shifted, inverted.len, remainder.len, data);
}

// A shift wraps at its operands' type in every build (§6.2: the count is
// masked, and the result is truncated back to the width), so a constant one
// folds to the value the same shift computes at run time -- including where
// that leaves the type's range, which no shift aborts on.
let K: i8 = 65;

fn shifts() {
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

fn main() {
    extents();
    shifts();
}
