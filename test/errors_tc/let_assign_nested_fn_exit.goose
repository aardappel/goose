// error: cannot assign to let x, which may be assigned already
// init assigns x at its early return, and not at the end of its body: after
// the call, x may be assigned as some exit of the body left it (§4.4, §7.5).
fn run(c: bool) {
    let x: i64;
    fn init(d: bool) {
        if d {
            x = 5;
            return;
        }
    }
    init(c);
    x = 6;
    print(x);
}
fn main() { run(true); }
