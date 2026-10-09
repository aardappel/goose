// runtime-debug: an in-order float sum whose terms are computed a block at a time (optimize_loops.h) aborts at the term the loop as written aborts at, partway through a block.

fn total(w: f64[:], n: i64) -> f64 {
    var s = 0.0;
    for j in n { s += w[j] * 2.0; }
    return s;
}

fn main() {
    var w: f64[>..] = [];
    for k in 13 { w.push(k * 0.5); }
    print(total(w, 13));
    print(total(w, 20));
}
