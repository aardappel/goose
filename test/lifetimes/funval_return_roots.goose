// A block's return out of the functions it is handed to (§7.6, §7.9) gives
// the function it returns from what returning out of each of those calls
// would: a reference the calls hand the block points where their arguments
// did. So it may carry references rooted at that function's parameters,
// which its callers map onto their arguments, at a variable of the function
// it is nested in, or at a global. The rejected shapes are
// errors_tc/funval_return_*.
var G: i64 = 7;

fn apply<F>(n: i64) -> i64 { F(n); return 0; }
fn each<F>(n: i64) { for i in n { F(i); } }
fn each_elem<F>(xs: i64[:]) { for i in xs.len { F(&xs[i]); } }
fn each_via<F>(xs: i64[:]) { each_elem(xs) { y: i64& => F(y); }; }

// Rooted at pick's parameter: the caller's array.
fn pick(xs: i64[:], k: i64) -> i64& {
    apply(k) { i => return xs[i]; };
    return xs[0];
}

// Through an iterating function, or a global where nothing is found.
fn first_above(xs: i64[:], limit: i64) -> i64& {
    each(xs.len) { if xs[it] > limit { return xs[it]; } };
    return G;
}

// The element each_elem hands the block, and the one each_via hands it from
// each_elem's: both point into xs.
fn first_even(xs: i64[:]) -> i64& {
    each_elem(xs) { x: i64& => if x % 2 == 0 { return x; } };
    return G;
}
fn last_below(xs: i64[:], limit: i64) -> i64& {
    var found: i64 = -1;
    each_via(xs) { x: i64& => if x < limit { found = x; } };
    each_via(xs) { x: i64& => if x == found { return x; } };
    return G;
}

// A holder whose slice is rooted at the parameter.
struct Tail { s: i64[:] }
fn tail_from(xs: i64[:], k: i64) -> Tail {
    apply(k) { i => return Tail { s: xs[i..] }; };
    return Tail { s: xs };
}

// The function the block is handed to is the target: its own parameter.
fn search<F>(xs: i64[:]) -> i64& { F(xs); return xs[0]; }

// A variable of the function the target is nested in outlives the target.
fn outer() -> i64 {
    var local = [10, 20, 30];
    fn inner(k: i64) -> i64& {
        apply(k) { i => return local[i]; };
        return local[0];
    }
    let r: i64& = inner(2);
    r = r + 3;
    return local[2];
}

fn main() {
    var a: i64[>..] = [5, 6, 7, 8];
    let r: i64& = pick(a, 2);
    print(r);
    r = 70;
    print(a[2]);
    let f: i64& = first_above(a, 6);
    print(f);
    print(first_above(a, 100));
    let e: i64& = first_even(a);
    e = 60;
    print(a[1]);
    let l: i64& = last_below(a, 60);
    l = 50;
    print(a[3]);
    let t = tail_from(a, 1);
    print(t.s.len);
    print(t.s[1]);
    let s: i64& = search(a) { v => return v[3] from search; };
    print(s);
    print(outer());
}
