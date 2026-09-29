// error: variable n may be used before it is assigned
// The else branch's call reuses the then branch's check of setn, and leaves
// n as that check's exits all do: unassigned, as its early return leaves it
// (§4.4, §7.5).
fn run(c: bool, x: bool) {
    var n: i64;
    fn setn(d: bool) { if d { return; } n = 5; }
    if x {
        setn(c);
        n = 7;
    } else {
        setn(c);
    }
    print(n);
}
fn main() { run(false, false); run(true, false); }
