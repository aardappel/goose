// error: constant 257 does not fit u8
// A range's end may be one past its binder type's largest value, and no
// further (§6.5): 0..256 visits every u8, 0..257 would need a 256.
fn main() {
    for i: u8 in 0..257 {
        print(i);
    }
}
