// error: variable n may be used before it is assigned
// setn's block breaks out before n is assigned, so the body ends with n
// unassigned on that path, and the call leaves it so (§4.4, §7.5).
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        block {
            if d { break; }
            n = 5;
        }
    }
    setn(c);
    print(n);
}
fn main() { run(false); run(true); }
