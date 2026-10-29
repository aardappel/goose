// runtime-debug: repeated owners, partial initialization and inlined copies.
root struct State {
    first: i64[>..],
    count: i64,
    second: i64[>..]
}

fn run(stop: bool) -> i64 {
    var s = State { [10], if stop { return -1; } else { 2 }, [20] };
    s.first.push(s.count);
    s.second.push(3);
    return s.first[1] + s.second[1];
}

fn main() {
    print(run(true), " ", run(false), " ", run(false));
    var total = 0;
    for i in 10 {
        var s = State { .. };
        s.first.push(i);
        s.second.push(i + 1);
        if i % 2 == 0 { continue; }
        total += s.first[0] + s.second[0];
        if i == 7 { break; }
    }
    print(total);
}
