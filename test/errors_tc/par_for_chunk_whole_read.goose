// error: a is partitioned by this par for, each iteration owning a.chunk(k, 4)
// R2 (§4.4): an array an iteration writes a chunk of is not read whole in
// the body, here by a function that walks it, since other iterations are
// writing the rest of it.
fn total(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}

fn f(a: i64[:]) {
    par for k in 4 {
        let c = a.chunk(k, 4);
        c[0] = total(a);
    }
}

fn main() {
    var xs = [1, 2, 3, 4, 5, 6, 7, 8];
    f(xs);
    print(xs);
}
