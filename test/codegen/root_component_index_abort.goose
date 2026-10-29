root struct State { items: i32[>..] = [] }
var state = State {};
type Id = u32 in state.items;
fn convert(n: i64) -> Id { n as Id }
fn main() {
    state.items.push(9);
    print(convert(0));
    print(convert(1));
    print("unreachable");
}
