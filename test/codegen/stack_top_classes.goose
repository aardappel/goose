// Stack tops (and counts) kept in locals, class by class, where another
// spelling of the same stack could see a stale value: a function's own
// stacks beside reference parameters, an inlined callee's `A&` parameter or
// a base-case inlining's copy of a reference argument standing for the
// variable it is bound to, a nested function's captured array, a function's
// return destination, the counts of reference parameters, globals and
// captured arrays, and the fields of a value built at a stack no class
// caches. Each case grows the stack around calls that grow, read or rebuild
// it under another name, and prints what every name sees.

struct P { a: i32, b: i32 }
enum Op { Num { v: i64 }, Add { l: i64, r: i64 }, Neg { x: i64 } }

var glob: i64[>..] = [];
var words: u8[>..] = [];

fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }
fn bump(xs: i64[>..]&, v: i64) { xs.push(v); }
fn glob_len() -> i64 { return glob.len; }
fn glob_push(v: i64) { glob.push(v); }

// Own stacks cached beside a reference parameter: the local's pushes in a
// loop around calls handed the local, and the parameter's around calls
// handed the parameter.
fn own_beside_param(p: i64[>..]&, n: i64) -> i64 {
    var mine: i64[>..] = [];
    for i in n {
        mine.push(i);
        if i % 3 == 0 { bump(mine, 100 + i); }
        p.push(i * 2);
        if i % 4 == 0 { bump(p, -i); }
    }
    return total(mine) * 1000 + mine.len;
}

// A reference variable that is not an alias may name one of the function's
// own stacks, and a reference parameter a global: neither those stacks nor
// the globals' are cached then. (The recursion keeps the first a function
// of its own at -O2, where it would leave main caching nothing.)
recursive fn own_and_ref(c: bool, depth: i64) -> i64 {
    if depth > 0 { return own_and_ref(c, depth - 1); }
    var a: i64[>..] = [];
    let r: i64[>..]& = if c { a } else { a };
    for i in 3 { a.push(i); r.push(i * 10); a.push(r.len); }
    return total(a) * 100 + a.len;
}
fn glob_and_param(p: i64[>..]&) {
    glob.push(1);
    p.push(2);
    glob.push(3);
    p.push(glob.len);
}

// A call handed a reference to one local syncs that local's stack and no
// other; the other's top stays in its local across the call.
fn handed() -> i64 {
    var a: i64[>..] = [];
    var b: i64[>..] = [];
    for i in 4 {
        a.push(i);
        bump(b, i * 10);
        a.push(b.len);
        bump(a, -1);
        b.push(a.len);
    }
    return total(a) * 1000 + total(b);
}

// Inlined with xs bound to the caller's local or parameter: no reference
// variable is left, and the pushes use the variable's own stack.
fn push_n2<A, T>(xs: A&, v: T, n: i64) { for i in n { xs.push(v); xs.push(v); } }
fn heapish(xs: P[>..<]&, v: P) {
    xs.push(v);
    var i = xs.len - 1;
    while i > 0 && xs[(i - 1) / 2].a > xs[i].a {
        let t = xs[i];
        xs[i] = xs[(i - 1) / 2];
        xs[(i - 1) / 2] = t;
        i = (i - 1) / 2;
    }
}
fn aliases(p: i64[>..]&) -> i64 {
    var local: i64[>..] = [];
    push_n2(local, 7, 3);
    push_n2(p, 5, 2);
    var h: P[>..<] = [];
    for k in 6 { heapish(h, P { a: (10 - k) as i32, b: k as i32 }); }
    var popped = 0;
    while h.len > 0 { popped = popped * 10 + h[0].b; h.pop(); }
    return total(local) * 100000 + popped;
}

// Bound to a frame object's tail, from a local holding the object and from
// a parameter referring to one.
struct Bag { tag: i64, items: i64[>..] }
fn tails(b: Bag&) -> i64 {
    var mine = Bag { tag: 3, items: [] };
    push_n2(mine.items, 4, 2);
    mine.items.push(mine.items.len);
    push_n2(b.items, 6, 1);
    b.items.push(b.items.len);
    push_n2(b.items, mine.items.len, 1);
    return total(mine.items) * 100 + b.items.len;
}

// Not through a reference field, which can be rebound while the reference
// taken through it still names the first array.
struct Pair { r: Bag&, own: i64[>..] }
fn through_field(x: Bag&, y: Bag&) -> i64 {
    var p = Pair { r: x, own: [] };
    let d: i64[>..]& = p.r.items;
    p.r .= y;
    d.push(51);
    p.r.items.push(52);
    d.push(53);
    return x.items.len * 10 + y.items.len;
}

// Base-case inlining copies the pool argument into the inlined body.
struct Tree { item: i32, kids: i32 }
recursive fn build(pool: Tree[>..]&, depth: i32) -> i32 {
    if depth == 0 { return pool.push(Tree { item: depth, kids: 0 }).kids + 1; }
    pool.push(Tree { item: depth, kids: 2 });
    let a = build(pool, depth - 1);
    let b = build(pool, depth - 1);
    return a + b + 1;
}

// A function value's reference parameter bound to a reference parameter.
fn apply<F>(xs: i64[>..]&) { F(xs); }
fn through_value(p: i64[>..]&) {
    apply(p) { q => q.push(41); q.push(42); bump(q, 43); q.push(q.len); };
}

// A nested function's captured array, grown by recursion and by a sibling
// that captures it too, its count read by a third.
fn captured(n: i64) -> i64 {
    var acc: i64[>..] = [];
    fn count() -> i64 { return acc.len; }
    fn add(v: i64) { acc.push(v); }
    recursive fn walk(k: i64) {
        if k == 0 { return; }
        acc.push(k);
        add(k * 10);
        acc.push(count());
        walk(k - 1);
        acc.push(-k);
    }
    walk(n);
    return total(acc) * 100 + acc.len;
}

// A captured array a `return ... from` lands over (its function's named
// result), grown in the function value that returns.
fn each<F>(n: i64) { for i in n { F(i); } }
fn early(n: i64) -> u8[] {
    var s: u8[>..] = [];
    s.push('a');
    each(n) { i =>
        s.push('b');
        if i == 2 { return copy(s) from early; }
    };
    s.push('z');
    return s;
}

// The return destination's top in a local: a named result pushed in a
// loop around a call handed another stack, a call that touches a global and
// one that may return to a target above.
fn give_up(k: i64) -> i64 {
    if k == 15 { return str("gave up at ", k) from spelled; }
    return 0;
}
fn digits(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    var k = n;
    for i in 12 {
        out.push(('0' + k % 10) as u8);
        k = k / 10 + give_up(k);
        glob_push(i);
        if k == 0 { break; }
    }
    return out;
}
fn spelled(n: i64) -> u8[>..] { return digits(n); }

// Counts kept in locals: a reference parameter's around calls reading it,
// a global's around a callee that reads and one that pushes, a captured
// one's around a nested reader.
fn param_count(out: u8[>..]&, s: u8[:]) -> i64 {
    var seen = 0;
    for c in s {
        out.push(c);
        if c == ' ' { seen += out.len; out.push('_'); }
    }
    return seen + out.len;
}
fn glob_count(n: i64) -> i64 {
    var t = 0;
    for i in n {
        glob.push(i);
        t += glob_len();
        if i % 2 == 0 { glob_push(i * 100); }
        t += glob.len;
    }
    return t;
}
fn grown() -> u8[>..]& { return words; }
fn word_count(n: i64) -> i64 {
    var t = 0;
    for i in n {
        words.push(('a' + i % 26) as u8);
        let w = copy(words);
        let g = copy(grown());
        t += w.len + words.len + g.len * 100;
    }
    return t;
}

// Fields written through a local where no class caches the stack: a
// captured reference's, around a field initializer that calls a function
// pushing elsewhere and one reading the array being built on, and a
// reference variable's, with a field the call builds in place there.
struct Rec { id: i64, name: u8[varint], tag: i64 }
fn label(k: i64) -> u8[>..] {
    var s: u8[>..] = [];
    for i in k + 1 { s.push(('a' + i) as u8); }
    return s;
}
recursive fn ops(n: i64, depth: i64) -> i64 {
    if depth > 0 { return ops(n, depth - 1); }
    var prog: Op[>..] = [];
    var side: i64[>..] = [];
    let r: Op[>..]& = if n > 0 { prog } else { prog };
    fn emit(k: i64) {
        r.push(Op.Add { l: k, r: side.len });
        bump(side, k);
        r.push(Op.Neg { x: r.len });
        r.push(Op.Num { v: total(side) });
    }
    for k in n { emit(k); }
    var recs: Rec[>..] = [];
    let rr: Rec[>..]& = if n > 0 { recs } else { recs };
    for k in n { rr.push(Rec { id: k, name: label(k), tag: rr.len }); }
    for x in recs { side.push(x.id * 100 + x.name.len * 10 + x.tag); }
    var t = total(side);
    for &o in prog {
        match o {
            Num x => { t += x.v; },
            Add a => { t += a.l * 3 + a.r; },
            Neg g => { t -= g.x; },
        }
    }
    return t * 1000 + prog.len;
}

// Fills: equal bytes as one memset, others element by element. Small
// aggregates pushed whole, and read back whole.
struct H { a: i16, b: u8, c: u8 }
fn fills() -> i64 {
    var hs: H[>..<] = [];
    var lim: P[..4] = [];
    for i in 3 {
        hs.push(H { a: (i - 2) as i16, b: (i + 3) as u8, c: 9 });
        lim.push(P { a: i as i32, b: (i * 2) as i32 });
    }
    let top = hs[hs.len - 1];
    hs.pop();
    let low = lim[lim.len - 1];
    var b: bool[>..] = [];
    b.resize(9, true);
    var m: i32[>..] = [];
    m.resize(5, -1);
    var z: P[>..] = [];
    z.resize(4, P { a: 0, b: 0 });
    var q: P[>..] = [];
    q.resize(3, P { a: 2, b: 3 });
    q.resize(5, P { a: 7, b: 7 });
    var t = 0;
    for x in b { if x { t += 1; } }
    for x in m { t += x; }
    for x in z { t += x.a + x.b; }
    for x in q { t = t * 10 + x.a; }
    return t * 1000 + top.a * 100 + top.b * 10 + low.b + hs.len;
}

fn main() {
    var p: i64[>..] = [];
    print(own_beside_param(p, 9), " ", p);
    print(own_and_ref(true, 1), " ", handed());
    glob_and_param(glob);
    print(glob);
    glob.clear();
    var p2: i64[>..] = [];
    print(aliases(p2), " ", p2);
    var bag = Bag { tag: 1, items: [7] };
    print(tails(bag), " ", bag.items);
    var bag2 = Bag { tag: 2, items: [] };
    print(through_field(bag, bag2), " ", bag.items, " ", bag2.items);
    var pool: Tree[>..] = [];
    print(build(pool, 4), " ", pool.len, " ", pool[0].kids, " ", pool[pool.len - 1].item);
    var p3: i64[>..] = [1];
    through_value(p3);
    print(p3);
    print(captured(4));
    print(early(5), " ", early(1));
    print(spelled(98765), " ", spelled(150), " ", glob.len);
    glob.clear();
    var out: u8[>..] = [];
    print(param_count(out, "ab cd e"), " ", out);
    print(glob_count(6), " ", glob);
    print(word_count(5), " ", words);
    print(ops(4, 1));
    print(fills());
}
