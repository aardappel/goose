// A spawn whose id is thrown away still starts the worker.
struct Done { n: i32 }
thread_fn worker() { qput(Done { n: 42 }); }
fn main() {
    thread_spawn(worker);
    print(qget<Done>().n);
}
