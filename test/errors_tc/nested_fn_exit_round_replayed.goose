// error: variable n may be used before it is assigned
// once's check reuses bail's, which leaves setn before n is assigned, so
// once's check takes that exit too; setn's second round reuses it, and takes
// the exit again (§4.4, §7.8, §7.9).
fn once<F>() { F(); }
fn run(c: bool) {
    var n: i64;
    recursive fn setn(d: i64) {
        fn bail(e: bool) { if e { return from setn; } }
        if d > 5 { n = 1; bail(d == 0); } else { once() { bail(d == 0); } }
        n = 5;
        if d > 0 { setn(d - 1); }
    }
    setn(if c { 0 } else { 1 });
    print(n);
}
fn main() { run(false); run(true); }
