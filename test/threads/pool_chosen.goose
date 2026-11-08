// The ways to pick or hand on a pool that make no second handle to it, as
// the copies errors_tc/pool_copy_*.goose would:
// - a construct picking between pools, handed to a thread_pool& parameter,
//   which binds the pick by reference: closing through it closes the
//   variable picked;
// - a construct whose branches each make a pool, and a function value whose
//   block makes one: each makes the pool it gives;
// - a multi-value result bound by reference (.=), and an inferred result
//   that is a reference (&p).
fn fill(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx + 1; }
}

fn run_on(p: thread_pool&, data: i64[:]) {
    p.apply(chunks(data, 2), fill);
}

fn end_pool(p: thread_pool&) {
    p.close();
}

fn both(p: thread_pool&) -> thread_pool&, i64 {
    return p, 3;
}

fn get(p: thread_pool&) {
    &p
}

fn made<F>(data: i64[:]) {
    var q = F();
    defer q.close();
    q.apply(chunks(data, 1), fill);
}

fn main() {
    var a = thread_pool(2);
    var b = thread_pool(2);
    defer b.close();
    var data: i64[>..] = [0, 0, 0, 0];
    for i in 2 {
        run_on(if i == 0 { a } else { b }, data);
    }
    print(data);
    let q, n .= both(b);
    q.apply(chunks(data, n), fill);
    print(data);
    get(b).apply(chunks(data, 4), fill);
    print(data);
    let first = true;
    end_pool(if first { a } else { b });
    a.close();   // closed already through end_pool: nothing to do
    let many = true;
    var c = if many { thread_pool(4) } else { thread_pool(1) };
    defer c.close();
    c.apply(chunks(data, 1), fill);
    print(data);
    made(data) { thread_pool(2) };
    print(data);
}
