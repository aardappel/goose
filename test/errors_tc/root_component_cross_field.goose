// error: a relative reference in state.items must point into state.items
// error: this reference is rooted at state.other
struct Node { next: Node&<u32 in state.items>? }
root struct State { items: Node[>..] = [], other: Node[>..] = [] }
var state = State {};
fn main() {
    state.items.push(Node { null });
    state.other.push(Node { null });
    state.items[0].next .= state.other[0];
}
