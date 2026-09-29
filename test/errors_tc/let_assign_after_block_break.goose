// error: cannot assign to let x, which may be assigned already
// A block is left at its break as well as at its end: x may be assigned
// after it (§4.4, §6.4).
fn run(c: bool) {
    let x: i64;
    block {
        if c {
            x = 5;
            break;
        }
    }
    x = 6;
    print(x);
}
fn main() { run(true); }
