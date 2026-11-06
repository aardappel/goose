// error: close() requires a thread_pool, got i64[>..]
// The builtin close() ends thread pools, and nothing else.
fn main() {
    var xs: i64[>..] = [1, 2];
    xs.close();
    print(xs);
}
