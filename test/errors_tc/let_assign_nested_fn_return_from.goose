// error: cannot assign to let x, which may be assigned already
// bail assigns x and leaves setx: after setx's call, x may be assigned as
// that exit left it (§4.4, §7.5, §7.9).
fn run(c: bool) {
    let x: i64;
    fn setx(d: bool) {
        fn bail(e: bool) {
            if e {
                x = 1;
                return from setx;
            }
        }
        bail(d);
    }
    setx(c);
    x = 5;
    print(x);
}
fn main() { run(true); }
