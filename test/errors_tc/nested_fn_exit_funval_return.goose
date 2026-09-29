// error: variable n may be used before it is assigned
// The block's return leaves setn, not each, before n is assigned: an exit
// of setn taken in each's body (§4.4, §7.6).
fn each<F>(k: i64) { for i in k { F(i); } }
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        each(2) { if d { return; } }
        n = 5;
    }
    setn(c);
    print(n);
}
fn main() { run(false); run(true); }
