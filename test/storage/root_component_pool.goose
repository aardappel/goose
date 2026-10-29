// Each named component has its own base, including through owner parameters.
import slot_pool;

struct Node {
    value: i32,
    left: Node&<u32 in state.left>?,
    right: Node&<u16 in state.right>?
}
root struct State<T> { left: T[>..] = [], right: T[>..] = [] }
var state = State<Node> {};

fn connect(s: State<Node>&) {
    s.left[0].left .= s.left[1];
    s.left[0].right .= s.right[0];
}
fn forward(s: State<Node>&) { connect(s); }
fn uncalled(s: State<Node>&) { connect(s); }
fn read(n: Node&) -> i32 {
    let a = n.left;
    let b = n.right;
    assert(a && b);
    return a.value + b.value;
}

struct Item { value: i32, next: Item&<u32 in items.items>? }
var items = slot_pool<Item> {};
fn add(p: slot_pool<Item>&, n: Item&) -> Item& {
    return p.alloc_ref(Item { value: n.value + 1, next: n });
}

struct Seed { value: i32, next: Seed&<u32 in seeds.items> }
var seeds = slot_pool<Seed> { items: [Seed { value: 9, next: self }], .. };

fn main() {
    state.left.push(Node { value: 3, .. });
    state.left.push(Node { value: 7, .. });
    state.right.push(Node { value: 11, .. });
    forward(state);
    print(read(state.left[0]));
    let first .= items.alloc_ref(Item { value: 20, next: null });
    let second .= add(items, first);
    let next = second.next;
    assert(next);
    print(second.value, " ", next.value);
    items.free(items.items.index_of(first));
    let reused .= items.alloc_ref(Item { value: 30, next: second });
    print(items.items.index_of(reused), " ", next.value);
    print(seeds.items[0].next.value);
}
