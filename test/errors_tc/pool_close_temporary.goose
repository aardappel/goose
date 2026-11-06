// error: close() requires a thread_pool variable or a reference to one, not a temporary
// close() leaves the variable holding a closed pool, and a temporary pool is
// in none.
fn main() {
    thread_pool(2).close();
}
