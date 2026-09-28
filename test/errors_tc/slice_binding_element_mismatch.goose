// error: expected a value of type const u8[:], got i64[3]
// Only an array of the slice's element type is a slice of itself (§3.10).
fn main() {
    var nums: i64[3] = [1, 2, 3];
    let s: const u8[:] = nums;
    print(s);
}
