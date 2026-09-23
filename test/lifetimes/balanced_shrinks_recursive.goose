// Recursive code keeps growable scratch outside the cycle and passes it down
// (§7.8); with a watermark, each activation leaves it as long as it found it,
// so the calls back into the cycle are balanced (§5.2) and a view an
// activation took of its own part stays in use across them. The same holds
// through a second function of the cycle, over a local of an enclosing
// function, over a global, with two calls back per activation, with a
// balanced callee outside the cycle called after the call back, and for a
// reference to the slice variable holding the view. A parameter's view of
// the global stays in use across the calls back, directly or through a
// finished member of the cycle, whatever the caller passes for it: once the
// calls back are balanced they record nothing for the callers to judge
// (§5.1).
var shared: i64[>..<] = [];

recursive fn walk(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    scratch.push(depth * 10);
    let mine = scratch[mark..];
    var total = 0;
    if depth < 3 { total = walk(scratch, depth + 1); }
    total += mine[0] + mine[1];
    scratch.resize(mark);
    return total;
}

recursive fn through(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    var mine = scratch[mark..];
    let held = &mine;
    var total = 0;
    if depth < 3 { total = through(scratch, depth + 1); }
    total += held[0];
    scratch.resize(mark);
    return total;
}

recursive fn down(s: i64[>..<]&, n: i64) -> i64 {
    let mark = s.len;
    s.push(n);
    let mine = s[mark..];
    let deeper = across(s, n - 1);
    let got = mine[0] + deeper;
    s.resize(mark);
    return got;
}
recursive fn across(s: i64[>..<]&, n: i64) -> i64 {
    if n <= 0 { return 0; }
    return down(s, n);
}

fn nested(n: i64) -> i64 {
    var scratch: i64[>..<] = [];
    recursive fn go(d: i64) -> i64 {
        let mark = scratch.len;
        scratch.push(d);
        let mine = scratch[mark..];
        var t = 0;
        if d < n { t = go(d + 1); }
        t += mine[0];
        scratch.resize(mark);
        return t;
    }
    scratch.push(7);
    let r = go(1);
    return r * 10 + scratch[0];
}

recursive fn global_walk(d: i64) -> i64 {
    let mark = shared.len;
    shared.push(d);
    let mine = shared[mark..];
    var t = 0;
    if d < 3 { t = global_walk(d + 1); }
    t += mine[0];
    shared.resize(mark);
    return t;
}

recursive fn viewing(s: i64[:], d: i64) -> i64 {
    let mark = shared.len;
    shared.push(d);
    var t = 0;
    if d < 3 { t = viewing(s, d + 1); }
    t += s[0];
    shared.resize(mark);
    return t;
}

recursive fn forth(v: i64[:], n: i64) -> i64 {
    let mark = shared.len;
    shared.push(n);
    var t = 0;
    if n > 0 { t = back(v, n - 1); }
    t += v[0];
    shared.resize(mark);
    return t;
}
recursive fn back(v: i64[:], n: i64) -> i64 {
    return forth(v, n);
}

struct Node { l: i64, r: i64, val: i64 }
recursive fn paths(nodes: Node[:], i: i64, path: i64[>..<]&) -> i64 {
    if i < 0 { return 0; }
    let mark = path.len;
    path.push(nodes[i].val);
    let here = path[mark..];
    let below = paths(nodes, nodes[i].l, path) + paths(nodes, nodes[i].r, path);
    let total = below + here[0] * path.len;
    path.resize(mark);
    return total;
}

fn stamp(s: i64[>..<]&) {
    let mark = s.len;
    s.push(99);
    s.resize(mark);
}
recursive fn stamped(s: i64[>..<]&, d: i64) -> i64 {
    let mark = s.len;
    s.push(d);
    let mine = s[mark..];
    var t = 0;
    if d < 3 { t = stamped(s, d + 1); }
    stamp(s);
    t += mine[0];
    s.resize(mark);
    return t;
}

fn main() {
    var scratch: i64[>..<] = [];
    scratch.push(100);
    let v = scratch[..];
    print(walk(scratch, 1), " ", v[0], " ", scratch.len);
    print(down(scratch, 4), " ", v[0], " ", scratch.len);
    print(nested(4));
    shared.push(5);
    let g = shared[..];
    print(global_walk(1), " ", g[0], " ", shared.len);
    print(viewing(shared[..], 1), " ", forth(shared[..], 2), " ", shared.len);
    let nodes: Node[5] = [Node { 1, 2, 10 }, Node { 3, -1, 20 }, Node { -1, 4, 30 },
                          Node { -1, -1, 40 }, Node { -1, -1, 50 }];
    print(paths(nodes[..], 0, scratch), " ", v[0], " ", scratch.len);
    print(stamped(scratch, 1), " ", v[0], " ", scratch.len);
    print(through(scratch, 1), " ", v[0], " ", scratch.len);
}
