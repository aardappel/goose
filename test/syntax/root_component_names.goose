namespace named;
root struct State { items: i32[>..] = [] }
var state = State {};
type Link = i32&<u16 in state.items>;
type Id = u32 in named::state.items;
var ::state = named::State {};
type Other = u32 in ::state.items;
fn get(x: Id) -> i32 { state.items[x] }
fn ::main() {
    named::state.items.push(42);
    state.items.push(7);
    let link: named::Link = named::state.items[0];
    let i = 0 as named::Id;
    let j = 0 as named::Other;
    print(link, " ", named::get(i), " ", state.items[j]);
}
