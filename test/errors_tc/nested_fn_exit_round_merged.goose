// error: variable n may be used before it is assigned
// bail leaves setn twice, having assigned n before the first exit and not
// before the second; setn's second round reuses bail's check, which takes
// both again (§4.4, §7.8, §7.9).
fn run(c: bool) {
    var n: i64;
    recursive fn setn(d: i64) {
        fn bail(e: i64) {
            if e == 1 { n = 1; return from setn; }
            if e == 2 { return from setn; }
        }
        bail(d);
        n = 5;
        if d > 2 { setn(d - 1); }
    }
    setn(if c { 2 } else { 3 });
    print(n);
}
fn main() { run(false); run(true); }
