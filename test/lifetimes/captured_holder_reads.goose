// A reference read out of a holder of the function a nested function's or
// a function value's body is written in points where that holder's stores
// put it, as in the declaring body itself (§9.5): the body is checked again
// for a call that finds the holder holding something else (§7.5), so the
// links it stores into the caller's pool are rooted there.
struct Node { v: i64, first: Node? }
struct Cur { first: Node?, count: i64 }

fn twice<F>() { F(0); F(1); }

fn build(pool: Node[>..]&) -> i64 {
    var cur = Cur { first: null, count: 0 };
    cur.first .= pool.push(Node { v: 1, first: null });
    fn close() -> Node& { return pool.push(Node { v: 2, first: cur.first }); }
    let n .= close();
    let m .= pool.push(Node { v: 3, first: cur.first });
    twice() { r => pool.push(Node { v: 4 + r, first: cur.first }); };
    let a = n.first;
    let b = m.first;
    guard a else { return 0; }
    guard b else { return 0; }
    return a.v * 10 + b.v + pool.len * 100;
}

fn main() {
    var pool: Node[>..] = [];
    print(build(pool));
    for &n in pool {
        let f = n.first;
        if f { print(n.v, " -> ", f.v); }
    }
}
