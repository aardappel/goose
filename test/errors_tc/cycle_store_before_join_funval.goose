// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes p rooted differently from the cycle's entry call
// A function value's body runs inside the call that calls it, so its store
// is apply's, which the block's call back into f makes a member of f's cycle
// (§7.8): that the block is written in f makes no difference. The store
// relies on p being passed on around the cycle, and the call back passes f's
// local instead.
struct Cell { v: i64, link: Cell&? }

fn apply<F>(n: i64) -> i64 { return F(n); }

recursive fn f(p: Cell&, q: Cell&, n: i64) -> i64 {
    var local = Cell { v: 100 + n, link: null };
    return apply(n) { k =>
        q.link .= p;
        if k > 0 { f(local, q, k - 1) } else { 0 }
    };
}

fn main() {
    var a = Cell { v: 1, link: null };
    var b = Cell { v: 2, link: null };
    print(f(a, b, 2));
    let l = b.link;
    if l { print(l.v); }
}
