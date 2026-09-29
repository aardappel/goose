// error: cannot assign to let x, which may be assigned already
// The value is computed before x is assigned, and computing it assigns x
// (§4.4).
fn main() {
    let x: i64;
    fn init() -> i64 {
        x = 5;
        return 6;
    }
    x = init();
    print(x);
}
