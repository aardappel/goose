// error: cannot assign to let x, which may be assigned already
// The iteration after a continue finds x as the continue left it: assigned
// (§4.4, §6.5).
fn main() {
    let x: i64;
    for i in 3 {
        if i == 0 {
            x = 1;
            continue;
        }
        print(i);
    }
}
