// Compile-time extents use the same widths and signedness as runtime arithmetic.
let MAX: u8 = 255;
let ONE: u8 = 1;
let WORD: u64 = 18446744073709551615;
struct Data { value: i64[WORD >> 63] }

fn main() {
    let high: i64[18446744073709551615 >> 63] = [7];
    let quotient: i64[18446744073709551615 / 9223372036854775808] = [8];
    let wrapped: i64[MAX + 1] = [];
    let shifted: i64[ONE << 8] = [9];
    let inverted: i64[~MAX] = [];
    let remainder: i64[-9223372036854775808 % -1] = [];
    let data = Data { [10] };
    print(high, quotient, wrapped.len, shifted, inverted.len, remainder.len, data);
}
