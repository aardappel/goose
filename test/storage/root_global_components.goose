// runtime-debug: global components retain independent identities across calls.
root struct State { first: i64[>..], count: i64, second: i64[>..<] }
var state: State;

fn add(value: i64) -> i64& {
    state.first.push(value);
    state.count += 1;
    state.first[state.first.len - 1]
}

fn reset_work() {
    state.second.clear();
    state.second.push(state.count);
}

fn alias_global(a: i64[>..]&) {
    // The argument can be the same stack also reached through the global.
    state.first.push(11);
    a.push(13);
}

fn main() {
    let r .= add(7);
    reset_work();
    print(r, " ", state.first.index_of(r), " ", state.second[0]);
    let a = state.first;
    a.push(9);
    reset_work();
    print(state.first[1], " ", a.len, " ", state.count);
    alias_global(state.first);
    print(state.first[2], " ", state.first[3], " ", state.first.len);
}
