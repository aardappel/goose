// error: a is partitioned by this par for, each iteration owning a.chunk(k, 4)
// R2 (§4.4): an array an iteration writes a chunk of is reached no other way
// in the body, here by a read of another iteration's element.
fn f(a: i64[:]) {
    par for k in 4 {
        let c = a.chunk(k, 4);
        c[0] = a[0];
    }
}

fn main() {
    var xs = [1, 2, 3, 4, 5, 6, 7, 8];
    f(xs);
    print(xs);
}
