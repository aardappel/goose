// An abort on main's thread ends the program while contained workers run
// (§11.2). Expected to abort.
thread_fn idle() { qget<i64>(); }
fn main() {
    let other = thread_spawn_contained(idle);
    let xs = [1, 2, 3];
    print("aborting");
    print(xs[other + 3]);
    qput(0);
}
