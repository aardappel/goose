// runtime-debug all-levels
import slot_pool;
struct Node { value: i64, next: Node&<u32 in nodes.items>? }
var nodes = slot_pool<Node> {};

recursive fn make(n: i64) -> Node& {
    if n == 0 { return nodes.alloc_with() { Node { 0, null } }; }
    let tail: Node& = make(n - 1);
    return nodes.alloc_with() { Node { n, tail } };
}
fn release(p: slot_pool<Node>&, n: Node&) { p.free_ref(n); }
recursive fn destroy(n: Node&) {
    let tail = n.next;
    if tail { destroy(tail); }
    release(nodes, n);
}

struct Indices { a: u32 in first.items, b: u32 in second.items }
var first = slot_pool<i64> {};
var second = slot_pool<i64> {};
fn release_number(p: slot_pool<i64>&, value: i64&) { p.free_ref(value); }
fn main() {
    destroy(make(3));
    assert(nodes.items.len == 4 && nodes.free_slots.len == 4);
    assert(nodes.alloc_index_with() { Node { 9, null } } == 3);
    let a: i64& = first.alloc_ref(10);
    let b: i64& = second.alloc_ref(20);
    let indices = Indices { 0 as u32 in first.items, 0 as u32 in second.items };
    assert(first.items[indices.a] == 10 && second.items[indices.b] == 20);
    release_number(first, a);
    release_number(second, b);
    assert(first.alloc_index(30) == 0 && second.alloc_index(40) == 0);
    print("named pool reference frees: ok");
}
