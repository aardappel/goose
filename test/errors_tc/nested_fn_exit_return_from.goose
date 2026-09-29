// error: variable n may be used before it is assigned
// bail's `return from setn` leaves setn before n is assigned: an exit of
// setn taken in a callee's body (§4.4, §7.9).
fn run(c: bool) {
    var n: i64;
    fn bail(d: bool) { if d { return from setn; } }
    fn setn(d: bool) {
        bail(d);
        n = 5;
    }
    setn(c);
    print(n);
}
fn main() { run(false); run(true); }
