// runtime-debug: a value choosing among pools of one kind -- an if, a match, a
// block, what a block or a loop breaks with -- is a pool reference to the pool
// it gives (§5.4). Passed to a parameter, bound to a variable or rebound with
// .=, it carries that pool's freelist, so an allocation through it reuses what
// the chosen pool freed and never the other's. A run handed back to a slice
// pool chosen so may be either pool's, and is checked when the call runs, as
// it is through a variable a loop rebinds to another pool.

enum Pick { A, B { n: i64 } }

reusable var gpool: i64[>..] = [10, 11];
reusable var gpool2: i64[>..] = [20, 21];
let gfirst = false;
var gref: i64[>..]& = if gfirst { gpool } else { gpool2 };
reusable[] var gslices: i64[>..] = [];

fn alloc(p: i64[>..]&, v: i64) -> i64 { p.alloc_index(v) }

fn take(k: Pick.A, p: i64[>..]&, v: i64) -> i64 { p.alloc_index(v) }
fn take(k: Pick.B, p: i64[>..]&, v: i64) -> i64 { p.alloc_index(v + k.n) }

// Given pools, the parameters are pool references, and so is either of them.
fn either(p: i64[>..]&, q: i64[>..]&, first: bool, v: i64) -> i64 {
    alloc(if first { p } else { q }, v)
}

fn args() {
    reusable var a: i64[>..] = [1, 2, 3];
    reusable var b: i64[>..] = [4, 5, 6];
    a.free(1);
    b.free(2);
    let big = a.len > 2;
    print(alloc(if big { b } else { a }, 7), " ", alloc(if big { a } else { b }, 8));   // 2 1
    b.free(0);
    let k: Pick = Pick.B { n: 50 };
    print(take(k, match k { A => a, B x => if x.n > 10 { b } else { a } }, 9));        // 0
    a.free(2);
    print(alloc({ let z = a.len; if z > 5 { b } else { a } }, 10));                    // 2
    b.free(1);
    print(either(a, b, false, 11), " ", either(a, b, true, 12));                       // 1 3
    a.free(0);
    print(alloc(block { if big { break b; } break a; }, 13), " ",
          alloc(loop { break a; }, 14));                                                // 3 0
    b.free(2);
    print(alloc(if big { if a.len > 9 { a } else { b } } else { a }, 15));            // 2
    print(a, " ", b);                                               // [14, 8, 10, 12] [59, 11, 15, 13]
}

fn bindings(first: bool, limit: i64) -> i64 {
    reusable var a: i64[>..] = [1, 2, 3];
    reusable var b: i64[>..] = [4, 5, 6];
    a.free(0);
    b.free(1);
    var r: i64[>..]& = if first { a } else { b };
    let i = r.alloc_index(7);
    r .= match i { 0 => b, _ => a };
    let j = r.alloc_index(8);
    // A branch that returns gives no value: s is bound to the other's pool.
    let s: i64[>..]& = if j < limit { a } else { return -1; };
    s.free(1);
    print(i, " ", j, " ", s.alloc_index(9), " ", a, " ", b);
    return i * 10 + j;
}

// Each pass allocates in one pool what the other pass freed there.
fn loops() {
    reusable var a: i64[>..] = [1, 2];
    reusable var b: i64[>..] = [3, 4];
    var got = 0;
    for i in 4 {
        let r: i64[>..]& = if i % 2 == 0 { a } else { b };
        let o: i64[>..]& = if i % 2 == 0 { b } else { a };
        o.free(i % 2);
        got = got * 10 + r.alloc_index(i + 5);
    }
    print(got, " ", a, " ", b);                                             // 2010 [1, 7, 5] [8, 4]
}

fn globals() {
    gpool.free(0);
    gpool2.free(1);
    print(gref.alloc_index(22), " ", gpool, " ", gpool2);                   // 1 [10, 11] [20, 22]
}

fn captured() {
    reusable var a: i64[>..] = [1, 2];
    reusable var b: i64[>..] = [3, 4];
    var r: i64[>..]& = b;
    a.free(1);
    b.free(0);
    fn pick(first: bool, v: i64) -> i64 { alloc(if first { a } else { r }, v) }
    print(pick(false, 5), " ", pick(true, 6), " ", a, " ", b);              // 0 1 [1, 6] [5, 4]
}

// The pool the parameter was given, which may be gslices, or one of the
// function's own: a run of gslices may be the chosen pool's.
fn give_back(p: i64[>..]&, first: bool) -> i64 {
    reusable[] var mine: i64[>..] = [];
    let r: i64[>..]& = if first { p } else { mine };
    r.free_slice(gslices.alloc_slice(2));                                    // checked
    return r.alloc_slice(2).len * 10 + gslices.len;
}

fn slices() {
    reusable[] var s1: i64[>..] = [];
    reusable[] var s2: i64[>..] = [9];
    let t = s2.alloc_slice(2);
    s2.free_slice(t);
    let r: i64[>..]& = if s1.len > 0 { s1 } else { s2 };
    let u = r.alloc_slice(2);                                                // s2's freed run
    u[0] = 7;
    r.free_slice(u);
    let w = s2.alloc_slice(1);
    w[0] = 8;
    r.free_slice(w);                                                         // checked: it is s2's
    print(s1, " ", s2, " ", r.alloc_slice(3).len, " ", s2.len);              // [] [9, 8, 0] 3 4
    var q: i64[>..]& = s1;
    for i in 2 {
        let v = s2.alloc_slice(1);
        if i == 1 { q.free_slice(v); }                                       // checked: q is s2 by now
        q .= s2;
    }
    print(s2.len, " ", s2.alloc_slice(1).len, " ", s2.len);                  // 6 1 6
    print(give_back(if s1.len > 0 { s1 } else { gslices }, true), " ", give_back(gslices, true));   // 22 24
}

fn main() {
    args();
    print(bindings(true, 9), " ", bindings(false, 9), " ", bindings(true, 1));   // 1 10 -1
    loops();
    globals();
    captured();
    slices();
}
