// error: global saved may hold a reference into it
root struct State { first: i64[>..], second: i64[>..] }
var state = State { [1], [2] };
var saved: i64& = state.first[0];
fn main() { state.first.clear(); print(saved); }
