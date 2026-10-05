// Fixed values passed to a thread retain the internal indirect convention.
// The spawn packet is already the worker's private copy; queue traffic must
// also avoid large native temporaries and leave the main value unchanged.
struct Packet { bytes: u8[1200000], tag: i64 }
thread_fn worker(p: Packet) {
    p.bytes[1199999] += 1;
    p.tag += 1;
    qput(p);
}
fn main() {
    let original = Packet { tag: 7, .. };
    let t = thread_spawn(worker, original);
    let result = qget<Packet>();
    thread_wait(t);
    assert(result.tag == 8 && result.bytes[1199999] == 1);
    assert(original.tag == 7 && original.bytes[1199999] == 0);
    print("large fixed thread ok");
}
