// error: cannot assign to let x, which may be assigned already
// Each iteration after the first finds x assigned by the one before (§4.4,
// §6.5).
fn main() {
    let x: i64;
    for i in 3 {
        x = i;
        print(x);
    }
}
