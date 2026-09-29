// error: cannot assign to let x, which may be assigned already
// A let declared without a value is assigned once: after an if that assigns
// it in one branch, it may hold a value already (§4.4).
fn run(c: bool) {
    let x: i64;
    if c {
        x = 5;
        print(x);
    }
    x = 6;
    print(x);
}
fn main() { run(true); }
