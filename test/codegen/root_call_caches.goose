// runtime-debug: known root aliases share caches and sync across nested calls.
root struct State { first: i64[>..], second: i64[>..] }
var state = State { .. };

fn pair(a: State&, b: State&, n: i64) -> i64, i64 {
    for i in n { a.first.push(i); b.first.push(10 + i); }
    return a.first.len, b.first.len;
}
fn inner(s: State&, n: i64) -> i64, i64 {
    s.first.push(n);
    return s.first.len, s.second.len;
}
fn outer(s: State&, n: i64) -> i64, i64 {
    for i in n {
        s.first.push(20 + i);
        inner(s, 30 + i);
        s.second.push(s.first.len);
    }
    return s.first.len, s.second.len;
}
fn mixed(s: State&, a: i64[>..]&) -> i64, i64 {
    for i in 2 { s.first.push(40 + i); a.push(50 + i); }
    return s.first.len, a.len;
}
fn direct(s: State&) -> i64, i64 {
    for i in 2 { s.first.push(60 + i); state.first.push(70 + i); }
    return s.first.len, state.first.len;
}
fn main() {
    pair(state, state, 3);
    outer(state, 3);
    mixed(state, state.first);
    direct(state);
    print(state.first);
    print(state.second);
}
