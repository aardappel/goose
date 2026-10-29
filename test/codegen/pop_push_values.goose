// A pop's value and a push's reference read the array's top where they are
// used (B_POP, EmitPush), next to arguments and operands that push over the
// same slot before that use; a pop the length test before it makes safe
// (Call::nobc); a variable or constant stored straight to an element
// (Assign::CgStmt, QuietValue); and range tests taken as one unsigned
// comparison (RangeTest), in each spelling, against a length, a capacity
// and a constant, with negative, in-range and too-large values.
struct P { x: i32, y: i32 }

fn grow(a: i64[>..]&, v: i64) -> i64 {
    a.push(v);
    return v;
}

fn take(x: i64, y: i64) -> i64 { x * 10 + y }

fn sum(p: P&, extra: i64) -> i64 { p.x + p.y + extra }

fn more(ps: P[>..]&) -> i64 {
    ps.push(P { x: 100, y: 200 });
    return 1000;
}

fn classify(k: i64, q: P[:]) -> u8[] {
    var s: u8[>..] = [];
    s.append(if k < 0 || k >= q.len { "o" } else { "i" });
    s.append(if k >= 0 && k < q.len { "i" } else { "o" });
    s.append(if 0 > k || q.len <= k { "o" } else { "i" });
    s.append(if 0 <= k && q.len > k { "i" } else { "o" });
    s.append(if k < 0 || k >= 2 { "o" } else { "i" });
    return s;
}

fn narrow(k: i32, l: i64[..]&) -> bool { k >= 0 && k < l.cap }

fn main() {
    var a: i64[>..] = [1, 2, 3];
    print(take(a.pop(), grow(a, 9)), " ", a);
    print(a.pop() + grow(a, 7) * 100, " ", a);
    var s = P { x: 0, y: 0 };
    if a.len != 0 {
        let v = a.pop();
        s.x = v as i32;
    }
    if a.len >= 2 {
        s.y = (a.pop() * 3 + a.pop()) as i32;
    }
    print(s.x, " ", s.y, " ", a.len);

    var ps: P[>..] = [];
    let r = ps.push(P { x: 1, y: 2 });
    ps.push(P { x: 3, y: 4 });
    print(r.x, " ", r.y, " ", sum(ps.push(P { x: 5, y: 6 }), more(ps)), " ", ps.len);
    let top = ps.pop();
    print(top.x, " ", top.y, " ", ps.len);

    var q: P[>..] = [P { x: 1, y: 1 }, P { x: 2, y: 2 }, P { x: 3, y: 3 }];
    var i = 1;
    let w = P { x: 5, y: 6 };
    q[i] = w;
    i = 2;
    q[i] = q[0];
    q[i - 2] = w;
    var n = 7;
    var xs: i64[>..] = [0, 0, 0];
    xs[i] = n;
    n = 8;
    xs[i - 1] = n;
    for p in q { print(p.x, " ", p.y); }
    print(xs);

    for k in [-1, 0, 1, 2, 3, 100] { print(k, " ", classify(k, q)); }

    var l: i64[..] = [..4];
    l.push(5);
    l.push(6);
    while l.len > 0 { print(l.pop(), " ", l.len); }
    print(narrow(-1, l), " ", narrow(3, l), " ", narrow(4, l));
}
