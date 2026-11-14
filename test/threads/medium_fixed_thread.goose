// A fixed value of a few hundred bytes passed to a thread: by pointer into
// the spawn packet, the worker's private copy, as for a large one
// (large_fixed_thread.goose; CodeGen::PtrAbi). The main value is unchanged.
struct Packet { bytes: u8[300], tag: i64 }
thread_fn worker(p: Packet) {
    p.bytes[299] += 1;
    p.tag += 1;
    qput(p);
}
fn main() {
    let original = Packet { tag: 7, .. };
    let t = thread_spawn(worker, original);
    let result = qget<Packet>();
    thread_wait(t);
    assert(result.tag == 8 && result.bytes[299] == 1);
    assert(original.tag == 7 && original.bytes[299] == 0);
    print("medium fixed thread ok");
}
