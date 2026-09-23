// A recursive cycle's returns may give different roots too (§7.8, §9.2). A
// back edge is checked before the returns are, so it takes the roots the
// cycle's scan predicts they give, each mapped through its own arguments, and
// merges them as a call merges its callee's returns: here `build`'s back edge
// gives either of the two pools, rooted at the deeper, which is enough to
// write through it and to link it from an element of either.
struct Cell { v: i64, prev: Cell&? }

recursive fn build(a: Cell[>..]&, b: Cell[>..]&, n: i64) -> Cell& {
    if n == 0 { return a.push(Cell { v: 0, prev: null }); }
    let below: Cell& = build(a, b, n - 1);
    below.v += 10;
    return b.push(Cell { v: n, prev: below });
}

// A pool parameter or a global. Where the call passes the global itself,
// every return is rooted there, and so is the result.
var spare: Cell[>..] = [];
var last: Cell&? = null;

recursive fn find(p: Cell[>..]&, n: i64) -> Cell& {
    if n == 0 { return spare[0]; }
    if n == 1 { return p[0]; }
    let r: Cell& = find(p, n - 2);
    r.v += 1;
    return r;
}

// A string literal or a view of the input, the back edge's result returned
// as it is.
recursive fn nth_or(s: const u8[:], n: i64) -> const u8[:] {
    if s.len == 0 { return "(none)"; }
    if n == 0 { return s[..1]; }
    return nth_or(s[1..], n - 1);
}

// The scan does not follow an `if` returned whole: the first return checked
// stands in for the prediction, so the back edge after it has a writable
// result to write through.
recursive fn bump(p: Cell[>..]&, n: i64) -> Cell& {
    if n == 0 { return if p.len > 1 { p[1] } else { p[0] }; }
    let r: Cell& = bump(p, n - 1);
    r.v += 1;
    return r;
}

fn main() {
    var p: Cell[>..] = [];
    var q: Cell[>..] = [];
    let top: Cell& = build(p, q, 3);
    print(top.v, " ", p.len, " ", q.len);
    var c: Cell? = top;
    while c {
        print(c.v);
        c .= c.prev;
    }
    spare.push(Cell { v: 100, prev: null });
    var pool: Cell[>..] = [];
    pool.push(Cell { v: 200, prev: null });
    let even: Cell& = find(pool, 4);
    let odd: Cell& = find(pool, 5);
    print(even.v, " ", odd.v);
    last .= find(spare, 3);
    print(last.v);
    var word: u8[>..] = "goose";
    print(nth_or(word, 2), " ", nth_or(word, 7));
    print(bump(pool, 3).v);
}
