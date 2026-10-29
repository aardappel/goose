// error: a relative reference in state.items must point into state.items
struct Node { next: Node&<u32 in state.items>? }
root struct State { items: Node[>..] = [] }
var state = State {};
var other = State {};
fn connect(s: State&) { s.items[0].next .= s.items[0]; }
fn main() {
    state.items.push(Node { null });
    other.items.push(Node { null });
    connect(state);
    connect(other);
}
