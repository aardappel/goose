// runtime-debug: recursion, independent instances and builder freelist effects.
import slot_pool;

struct Node { value: i64, link: Node&<u32> }
var first = slot_pool<Node> {};
var second = slot_pool<Node> {};
var unique = slot_pool<Node> {};

fn add(p: slot_pool<Node>&, value: i64) -> i64, i64 {
    let r: Node& = p.alloc_with() { Node { value: value, link: self } };
    assert(r.link.value == value);
    return p.items.index_of(r), p.items.len;
}
recursive fn grow(p: slot_pool<Node>&, n: i64) {
    add(p, n);
    if n > 0 { grow(p, n - 1); }
}
recursive fn grow_unique(p: slot_pool<Node>&, n: i64) {
    p.alloc_with() { Node { value: n, link: self } };
    if n > 0 { grow_unique(p, n - 1); }
}
fn free_other() -> i64 {
    unique.free(1);
    return 80;
}
fn replace(p: slot_pool<Node>&) -> i64, i64 {
    let r: Node& = p.alloc_with() { Node { value: free_other(), link: self } };
    return p.items.index_of(r), p.free_slots.len;
}
fn mixed(a: slot_pool<Node>&, b: slot_pool<Node>&) -> i64, i64 {
    let x, _ = add(a, 90);
    let y, _ = add(b, 91);
    return x, y;
}
fn main() {
    grow(first, 3);
    grow(second, 1);
    var local = slot_pool<Node> {};
    grow(local, 2);
    first.free(1);
    second.free(0);
    let x, y = mixed(first, second);
    assert(x == 1 && y == 0);
    let a, b = mixed(first, first);
    assert(a == 4 && b == 5);
    grow_unique(unique, 2);
    unique.free(0);
    let i, pending = replace(unique);
    assert(i == 0 && pending == 1);
    let j, size = add(unique, 81);
    assert(j == 1 && size == 3);
    for &n in first.items { assert(n.link.value == n.value); }
    for &n in second.items { assert(n.link.value == n.value); }
    for &n in local.items { assert(n.link.value == n.value); }
    for &n in unique.items { assert(n.link.value == n.value); }
    print(first.items.len, " ", second.items.len, " ", local.items.len);
    print(unique.items[0].value, " ", unique.items[1].value, " ", unique.free_slots.len);
}
