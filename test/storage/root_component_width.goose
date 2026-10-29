// A compact link's width bounds its named component, including through calls.
root struct State { items: i32[>..] = [], links: (i32&<u8 in state.items>?)[>..] = [] }
var state = State {};
fn link(n: i32&) { state.links.push(n); }
fn main() {
    for i in 65 { state.items.push(i as i32); }
    link(state.items[63]);
    let last = state.links[0];
    assert(last);
    print(last);
    link(state.items[64]);
    print("unreachable");
}
