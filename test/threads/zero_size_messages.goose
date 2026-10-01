// Values of no bytes (§3.4) -- a variant type with no fields, a zero-length
// array -- cross queues and thread arguments as none, though C gives their
// objects one or more: the receiver zeroes its object rather than reading
// past the bytes sent. A struct of pads alone sends as many bytes as it
// lays out, which its C struct holds (§3.2). Under ASan a read past the
// arguments' image (the worker's last two, with no globals after them)
// shows at clang -O1, and one past a queue's node at -O0, where a read of
// bytes nothing uses is not dropped.
enum Mode { Off, Level { v: i64 } }
struct Pads { pad 4 }

thread_fn worker(n: i64, p: Pads, z: i64[0], o: Mode.Off) {
    qput(n + z.len);
    qput(p);
    qput(o);
}

fn main() {
    qput(Mode.Off {});
    let a = qget<Mode.Off>();
    let none: i64[0] = [];
    qput(none);
    let z = qget<i64[0]>();
    qput(Pads {});
    let p = qget<Pads>();
    let b, gotb = qpoll<Mode.Off>();
    qput(Mode.Off {});
    let c, gotc = qpoll<Mode.Off>();
    print(a, " ", z, " ", p, " ", b, " ", gotb, " ", c, " ", gotc);
    let id = thread_spawn(worker, 5, Pads {}, none, Mode.Off {});
    thread_wait(id);
    print(qget<i64>(), " ", qget<Pads>(), " ", qget<Mode.Off>());
}
