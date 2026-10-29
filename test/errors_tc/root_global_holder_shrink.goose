// error: global state may hold a reference
root struct S { items: i64[>..], refs: i64&[>..] }
var state = S { .. };
fn fill(s: S&) { s.items.push(1); s.refs.push(s.items[0]); }
fn clear(s: S&) { s.items.clear(); }
fn main() { fill(state); clear(state); print(state.refs[0]); }
