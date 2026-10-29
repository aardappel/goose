// runtime-debug: builders construct once in fresh or recycled slots.
import slot_pool;

struct Node { id: i64, me: Node&<u32>, next: Node&<u32>? }
struct Entry { value: i64& }
struct Pooled { id: i64, me: Pooled&<u32 in globals.items> }
var globals = slot_pool<Pooled> {};
var calls = 0;

fn linked(first: Node&, id: i64) -> Node { Node { id: id, me: self, next: first } }
fn forward<F>(pool: slot_pool<Node>&) -> Node& { pool.alloc_with(F) }
fn count() -> i64 { calls++; calls }
fn try_reuse(pool: slot_pool<Node>&, leave: bool) -> bool {
    pool.alloc_with() {
        if leave { return false from try_reuse; }
        Node { id: 99, me: self }
    };
    return true;
}

fn main() {
    var pool = slot_pool<Node> {};
    let first .= pool.alloc_with() { Node { id: count(), me: self } };
    let second .= forward(pool) { linked(first, count()) };
    assert(first.me .== first && second.me .== second);
    let next = second.next;
    assert(next && next .== first);
    pool.free(pool.items.index_of(second));
    let reused .= pool.alloc_with() { linked(first, count()) };
    assert(reused .== second && second.id == 3 && reused.me .== reused);
    assert(pool.items.len == 2 && calls == 3);
    let again = reused.next;
    assert(again && again .== first);
    pool.free(1);
    assert(!try_reuse(pool, true));
    assert(pool.items.len == 2 && pool.free_slots.len == 0 && second.id == 3);

    var value = 10;
    var other = 20;
    var entries = slot_pool<Entry> {};
    let entry .= entries.alloc_with() { Entry { value } };
    entries.free(0);
    entries.alloc_with() { Entry { other } };
    assert(entry.value .== other && value == 10);
    var refs = slot_pool<i64&> {};
    fn value_ref() -> i64& { return value; }
    fn other_ref() -> i64& { return other; }
    refs.alloc_with(value_ref);
    refs.free(0);
    refs.alloc_with(other_ref);
    assert(refs.items[0] .== other && value == 10);

    globals.alloc_with() { Pooled { id: 11, me: self } };
    globals.free(0);
    globals.alloc_with() { Pooled { id: 12, me: self } };
    assert(globals.items[0].me .== globals.items[0]);
    print("builders ", calls, " ", globals.items[0].id);
}
