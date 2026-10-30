// no-jit
struct CapPacket { tag: u8, view: SharedView }
fn view_of(p: CapPacket) -> SharedView { p.view }
fn first(v: SharedView[:]) -> SharedView { v[0] }
thread_fn packed_worker(tag: u8, view: SharedView) {
    assert(tag == 9);
    let access = shared_borrow(view);
    memory_write<u64>(access, 1, 1234, true); shared_release(access);
}
fn main() {
    let owner, ok = shared_alloc(64); assert(ok);
    let v = shared_view(owner, 32, 16);
    let packet = CapPacket { tag: 9, view: v };
    let views = [view_of(packet), v];
    assert(thread_wait(thread_spawn(packed_worker, packet.tag, first(views))));
    let access = shared_borrow(packet.view);
    assert(memory_read<u64>(access, 1, true) == 1234); shared_release(access);
    assert(shared_close(owner)); print("shared capability alignment ok");
}
