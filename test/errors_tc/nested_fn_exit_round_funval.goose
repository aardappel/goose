// error: variable n may be used before it is assigned
// setn's second round reuses the first round's check of each, whose block
// returns from setn before n is assigned: the reused check takes that exit
// again (§4.4, §7.6, §7.8).
fn each<F>(k: i64) { for i in k { F(i); } }
fn run(c: bool) {
    var n: i64;
    recursive fn setn(d: i64) {
        each(1) { if d == 0 { return; } }
        n = 5;
        if d > 0 { setn(d - 1); }
    }
    setn(if c { 0 } else { 1 });
    print(n);
}
fn main() { run(false); run(true); }
