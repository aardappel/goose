// An apply's arguments run before it hands its pool a chunk, and one of them
// may close the pool: the apply reads the pool from its variable once they
// have run, and aborts on a closed one, rather than handing chunks to what
// close freed. Here the chunk size closes it.
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn close_then_size(p: thread_pool&) -> i64 {
    p.close();
    return 1;
}

fn main() {
    var p = thread_pool(2);
    var xs: i64[>..] = [0, 0, 0, 0];
    print("applying");
    p.apply(chunks(xs, close_then_size(p)), fill);
    print(xs);
}
