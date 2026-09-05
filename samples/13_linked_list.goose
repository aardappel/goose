// A doubly-linked list the Goose way. The nodes live in a `reusable` pool:
// a grow-only array with a freelist, so removing a node hands its slot
// back and the next insertion takes it, with no allocator involved. The
// links are 4-byte offsets measured from the pool's base (`in pool`), and
// the list has a sentinel node whose links start out pointing at itself
// (`self`), so the list is never empty and no link is ever null: every
// insertion and removal is the same four stores, with no special cases.
//
// Build and run (from this directory):
//   goose -o linked_list.c 13_linked_list.goose && cl linked_list.c && linked_list

import std;

struct Node { val: i64, prev: Node&<u32 in pool>, next: Node&<u32 in pool> }

reusable var pool: Node[>..] = [];
// The sentinel: slot 0, allocated before main runs.
let head = pool.alloc_index(Node { val: 0, prev: self, next: self });

// Inserts a node holding v after p, and returns it.
fn insert_after(p: Node&, v: i64) -> Node& {
    // alloc_ref takes a free slot if there is one, else pushes; the links
    // are stored as offsets from the pool's base, so a node holds no
    // addresses at all.
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

// Unlinks n and frees its slot. The node keeps its bytes -- a stale
// reference would read a valid Node -- but the slot is reused next.
fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free(pool.index_of(n));
}

fn push_front(v: i64) -> Node& { insert_after(pool[head], v) }
fn push_back(v: i64) -> Node& { insert_after(pool[head].prev, v) }
// The sentinel test is an identity test: `==` on references compares the
// nodes they point at, `.==` the references themselves.
fn is_end(n: Node&) -> bool { n .== pool[head] }

fn pop_front() -> i64 {
    let n .= pool[head].next;
    assert(!is_end(n));
    let v = n.val;
    remove(n);
    return v;
}

// Visits every node, front to back.
fn each<F>() {
    var n .= pool[head].next;
    while !is_end(n) {
        let next .= n.next;              // read before F, which may remove n
        F(n);
        n .= next;
    }
}

fn show(label: u8[:]) {
    var line: u8[>..] = [];
    format(line, label, ":");
    each() { format(line, " ", it.val); };
    format(line, "   (", pool.len, " slots)");
    print(line);
}

fn main() {
    for i in 1..6 { push_back(i * 10); }
    push_front(5);
    show("built");

    // Removal while iterating is fine: the iteration reads the next link
    // before handing the node over.
    each() { if it.val % 20 == 0 { remove(it); } };
    show("evens removed");

    // Freed slots come back: three inserts, no growth.
    push_back(7);
    push_back(8);
    push_back(9);
    show("three more");

    print("popped ", pop_front(), " and ", pop_front());
    show("after pops");

    // Move-to-front, the operation an LRU cache is made of: unlink and
    // relink through the references, without touching the pool's length.
    var n .= pool[head].next;
    while !is_end(n) {
        let next .= n.next;
        if n.val == 9 {
            n.prev.next .= n.next;
            n.next.prev .= n.prev;
            n.prev .= pool[head];
            n.next .= pool[head].next;
            n.next.prev .= n;
            pool[head].next .= n;
        }
        n .= next;
    }
    show("9 moved to front");

    // Walk backwards through the prev links.
    var back: u8[>..] = [];
    var b .= pool[head].prev;
    while !is_end(b) { format(back, " ", b.val); b .= b.prev; }
    print("backwards:", back);
    print("a node is ", 8 + 4 + 4, " bytes: the value and two 4-byte links");
}
