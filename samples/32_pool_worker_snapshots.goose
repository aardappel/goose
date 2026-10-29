// A flat pool snapshot includes the initialized items and allocator metadata.
// The worker reconstructs its own root owners from ordinary copied arguments.
import slot_pool;
import slice_pool;

thread_fn worker(var items: i64[>..], var free_slots: i64[>..<],
                 var runs: i64[>..], var nodes: slice_pool_detail::node[>..],
                 tree: slice_pool_detail::tree) {
    var slots = slot_pool<i64> { items: copy(items), free_slots: copy(free_slots) };
    var slices = slice_pool<i64> { items: copy(runs), nodes: copy(nodes), tree: tree };
    assert(slots.alloc_index(20) == 0);
    let run = slices.alloc_slice(2);
    assert(slices.items.range_index(run) == 1);
    run[0] = 30;
    print("worker reused the copied slot and span");
}

fn main() {
    var slots = slot_pool<i64> {};
    slots.alloc_index(10); slots.free(0);
    var slices = slice_pool<i64> {};
    let run = slices.alloc_slice(4);
    run[1] = 11;
    slices.free_slice(run[1..3]);
    thread_wait(thread_spawn(worker, copy(slots.items), copy(slots.free_slots),
                             copy(slices.items), copy(slices.nodes), slices.tree));
    assert(slots.items[0] == 10 && run[1] == 11);
    assert(slots.alloc_index(40) == 0);
    assert(slices.items.range_index(slices.alloc_slice(2)) == 1);
    print("parent pools retain their own values and freelists");
}
