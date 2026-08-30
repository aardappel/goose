// Codegen execution test for threads and typed queues (§11.2). Deterministic:
// every qget blocks on exactly one qput, and joins order the rest.

struct Job { id: int, weight: i16 }

thread_fn worker(a: int, name: u8[]) {
    qput(a * 2);
    qput(name);                          // A variable-size flat value.
    qput(Job { id: a, weight: 7 });
}

fn main_codegen_threads() {
    let id = thread_spawn(worker, 21, "wk");
    print(qget<int>());                  // 42
    let s = qget<u8[]>();
    print(s);                            // wk
    let j = qget<Job>();
    print(j.id + j.weight);              // 28
    thread_wait(id);
    let v, ok = qpoll<int>();
    print(ok);                           // false
    print(v);                            // 0 (the zero value of a missed poll)
    qput(7);
    let v2, ok2 = qpoll<int>();
    print(v2);                           // 7
    print(ok2);                          // true
    print(hardware_threads() >= 1);      // true
    // A second worker, joined before reading, exercises wait-then-drain.
    let id2 = thread_spawn(worker, 1, "z");
    thread_wait(id2);
    print(qget<int>());                  // 2
    print(qget<u8[]>());                 // z
    print(qget<Job>().weight);           // 7
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_codegen_threads(); }
