// error: cannot assign to let x, which may be assigned already
// The second call reuses the first's check of init, and leaves x as that
// check did: maybe assigned (§4.4, §7.5).
fn run(c: bool, d: bool) {
    let x: i64;
    fn init(e: bool) { if e { x = 5; } }
    if c {
        init(d);
        return;
    }
    init(d);
    x = 6;
    print(x);
}
fn main() { run(false, true); }
