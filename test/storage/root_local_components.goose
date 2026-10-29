// runtime-debug: independent local components, including equal element types.
root struct State<T> {
    first: T[>..<],
    count: i64,
    second: T[>..<],
    text: u8[],
    tail: bool
}

fn add(a: i64[>..<]&, value: i64) { a.push(value); }

fn main() {
    var state = State<i64> { [10, 20], 7, [30], "ok", true };
    let kept .= state.first[0];
    state.second.clear();
    add(state.second, 40);
    state.count += 1;
    print(kept, " ", state.first.len, " ", state.second[0], " ", state.count,
          " ", state.text, " ", state.tail);
    let first = state.first;
    add(first, 50);
    print(state.first[2], " ", first.len);
}
