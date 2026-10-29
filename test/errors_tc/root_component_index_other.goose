// error: expected a value of type u32 in state.b, got u32 in state.a
root struct State { a: i32[>..] = [], b: i32[>..] = [] }
var state = State {};
fn main() {
    state.a.push(1);
    state.b.push(2);
    let a = 0 as (u32 in state.a);
    let b: u32 in state.b = a;
    print(b);
}
