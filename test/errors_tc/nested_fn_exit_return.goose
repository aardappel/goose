// error: variable n may be used before it is assigned
// setn's early return leaves n unassigned, where the end of its body does
// not: the call leaves n assigned only as every exit of the body does
// (§4.4, §7.5).
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        if d { return; }
        n = 5;
    }
    setn(c);
    print(n);
}
fn main() { run(false); run(true); }
