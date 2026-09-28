// Codegen execution test for threads and typed queues (§11.2). Deterministic:
// every qget blocks on exactly one qput, and joins order the rest.

struct Job { id: i64, weight: i16 }

// A flat resizable-tailed struct (a frame object, C.2) crosses whole.
struct Packet { id: i64, tag: u8, data: i64[>..] }
// Behind a variable-size prefix, one is bytes of its owner.
struct Wrapped { name: u8[], packet: Packet }

thread_fn worker(a: i64, name: u8[]) {
    qput(a * 2);
    qput(name);                          // A variable-size flat value.
    qput(Job { id: a, weight: 7 });
}

fn main_codegen_threads() {
    let id = thread_spawn(worker, 21, "wk");
    print(qget<i64>());                  // 42
    let s = qget<u8[]>();
    print(s);                            // wk
    let j = qget<Job>();
    print(j.id + j.weight);              // 28
    thread_wait(id);
    let v, ok = qpoll<i64>();
    print(ok);                           // false
    print(v);                            // 0 (the zero value of a missed poll)
    qput(7);
    let v2, ok2 = qpoll<i64>();
    print(v2);                           // 7
    print(ok2);                          // true
    print(hardware_threads() >= 1);      // true
    // A second worker, joined before reading, exercises wait-then-drain.
    let id2 = thread_spawn(worker, 1, "z");
    thread_wait(id2);
    print(qget<i64>());                  // 2
    print(qget<u8[]>());                 // z
    print(qget<Job>().weight);           // 7
    var p = Packet { id: 9, tag: 3, data: [10, 20, 30] };
    qput(p);
    let q = qget<Packet>();
    print(q.id, " ", q.tag, " ", q.data.len, " ", q.data[2]);   // 9 3 3 30
    let r, ok3 = qpoll<Packet>();
    print(ok3, " ", r.id, " ", r.data.len);                      // false 0 0
    var w = Wrapped { name: "w", packet: Packet { id: 4, tag: 5, data: [6] } };
    qput(w.packet);
    var w2 = Wrapped { name: "w2", packet: qget<Packet>() };
    w2.packet.data.push(7);
    print(w2.name, " ", w2.packet.id, " ", w2.packet.tag, " ", w2.packet.data);   // w2 4 5 [6, 7]
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_codegen_threads(); }
