// runtime-debug all-levels
import slot_pool;
struct Node { value: i64, next: Node&<u32>? }
fn main() {
    var p = slot_pool<Node> {};
    let a = p.alloc_index_with() { Node { 1, null } };
    let b = p.alloc_index_with() { Node { 2, p.items[a] } };
    assert(a == 0 && b == 1);
    p.free_ref(p.items[a]);
    let c = p.alloc_index_with() { Node { 3, p.items[b] } };
    assert(c == a);
    let link = p.items[b].next;
    if link { assert(link.value == 3); } else { abort("missing link"); }
    let held .= p.items[b];
    p.recycle_all();
    assert(p.items.len == 2 && held.value == 2);
    assert(p.alloc_index_with() { Node { 4, null } } == 0);
    assert(p.alloc_index_with() { Node { 5, p.items[0] } } == 1);
    assert(held.value == 5);
    p.recycle_all(); p.recycle_all();
    assert(p.free_slots.len == p.items.len);

    var values = slot_pool<i64> {};
    let index = values.alloc_index_with() { values.items.push(10); 20 };
    assert(index == 1 && values.items[index] == 20);
    values.free_ref(values.items[0]);
    assert(values.alloc_index_with() { values.items.push(30); 40 } == 0);
    assert(values.items == [40, 20, 30]);
    print("slot pool helpers: ok");
}
