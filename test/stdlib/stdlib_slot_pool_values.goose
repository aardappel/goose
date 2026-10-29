// runtime-debug: references, non-defaultable elements, global owners and aliases.
import slot_pool;

struct Cell { n: i64 }
struct Entry { cell: Cell&, text: const u8[:] }
struct Borrowed { pool: slot_pool<Entry>& }
var globals = slot_pool<i64> {};
var calls = 0;
fn argument() -> i64 { calls++; globals.free(0); return 7; }
fn refill(p: slot_pool<Entry>&, cell: Cell&, text: const u8[:]) -> Entry& {
    p.alloc_ref(Entry { cell, text })
}
fn through_holder(h: Borrowed&, cell: Cell&) -> Entry& { refill(h.pool, cell, "alias") }

fn main() {
    var a = Cell { 10 };
    var b = Cell { 20 };
    var refs = slot_pool<Cell&> {};
    let i = refs.alloc_index(&a);
    refs.free(i);
    assert(refs.alloc_index(&b) == i);
    assert(a.n == 10 && b.n == 20);
    let r .= refs.alloc_ref(&a);
    assert(r .== a && refs.items[0] .== b);

    var entries = slot_pool<Entry> {};
    var holder = Borrowed { entries };
    let first .= refill(entries, a, "first");
    let second .= through_holder(holder, b);
    entries.free(entries.items.index_of(first));
    refill(entries, b, "reused");
    assert(first.cell .== b && second.text == "alias");
    assert(a.n == 10 && first.text == "reused");

    globals.alloc_index(1);
    globals.alloc_index(2);
    globals.free(1);
    // Ordinary copyable arguments are evaluated before choosing a free slot.
    assert(globals.alloc_index(argument()) == 0);
    assert(calls == 1 && globals.alloc_index(8) == 1);
    var other = slot_pool<i64> {};
    other.alloc_index(99);
    assert(globals.items[0] == 7 && other.items[0] == 99);

    // A logical double free keeps memory typed; it can hand out the same slot twice.
    other.free(0); other.free(0);
    assert(other.alloc_index(100) == 0 && other.alloc_index(101) == 0);
    assert(other.items.len == 1 && other.items[0] == 101);
    print("values and aliases");
}
