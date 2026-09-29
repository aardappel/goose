// error: variable n may be used before it is assigned
// setn's tail value is an exit of its own, where n is unassigned, whatever
// the return before it assigns (§4.4, §7.5).
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) -> i64 {
        if d { n = 5; return 1; }
        2
    }
    print(setn(c));
    print(n);
}
fn main() { run(true); run(false); }
