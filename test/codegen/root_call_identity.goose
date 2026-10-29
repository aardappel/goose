// runtime-debug: call-site identity meets recursive edges and all pool instances.
root struct State { first: i64[>..], second: i64[>..] }
var one = State { .. };
var two = State { .. };

// Multiple results keep these helpers out of Goose's inliner.
fn known(s: State&) -> i64, i64 {
    s.first.push(10);
    s.second.push(20);
    return s.first.len, s.second.len;
}
fn forward(s: State&) -> i64, i64 { let a, b = known(s); return a, b; }
fn shared(s: State&, n: i64) -> i64, i64 {
    s.first.push(n);
    return s.first.len, s.second.len;
}
recursive fn descend(s: State&, n: i64) {
    s.first.push(n);
    if n > 0 { descend(s, n - 1); }
    s.second.push(n);
}
recursive fn shared_recursive(s: State&, n: i64) -> i64 {
    if n > 0 { return s.first.len + shared_recursive(s, n - 1); }
    return s.first.len;
}
fn dynamic(s: State&, n: i64) -> i64, i64 {
    s.first.push(n);
    return s.first.len, s.second.len;
}
fn main() {
    let alias .= one;
    forward(alias);
    shared(one, 30);
    shared(two, 40);
    descend(one, 3);
    assert(shared_recursive(one, 2) == 18);
    assert(shared_recursive(two, 2) == 3);
    var r .= one;
    let saved .= r;
    r .= two;
    dynamic(saved, 60);
    dynamic(r, 61);
    var local = State { .. };
    shared(local, 70);
    print(one.first, " ", one.second);
    print(two.first, " ", two.second, " ", local.first);
}
