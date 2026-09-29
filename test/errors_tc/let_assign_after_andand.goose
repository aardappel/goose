// error: cannot assign to let x, which may be assigned already
// The right operand of && may run: after the condition, what it assigns may
// be assigned (§4.4, §6.2).
fn run(c: bool) {
    let x: i64;
    fn setx() -> bool {
        x = 5;
        return true;
    }
    if c && setx() { print(1); }
    x = 6;
    print(x);
}
fn main() { run(true); }
