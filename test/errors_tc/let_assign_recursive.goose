// error: cannot assign to let x, which may be assigned already
// Every activation of rec may assign x, which is one variable outside them
// all (§4.4, §7.8).
fn run(c: bool) {
    let x: i64;
    recursive fn rec(n: i64) {
        if c { x = n; }
        if n > 0 { rec(n - 1); }
    }
    rec(3);
}
fn main() { run(true); }
