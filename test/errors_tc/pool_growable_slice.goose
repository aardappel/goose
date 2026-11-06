// error: chunks() of a growable output parameter T[>..]& appends to a grow-only or grow-shrink array (T[>..], T[>..<]), not i64[:]
// The tasks' output is appended to the array once they have all returned,
// which a slice of one cannot take.
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn fill(pool: thread_pool&, out: i64[:]) {
    pool.apply(chunks(out, 2), keep);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var xs: i64[>..] = [0, 0];
    fill(pool, xs);
}
