// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the first call of g
// f's store relies on its context being passed on around the cycle (§7.8).
// g's call back passes on what g is given, which its first call gave as f's
// context; by the second call g is a finished member of f's cycle, and that
// call gives it a local of f, which the call back then hands to f again.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, n: i64) -> i64 {
    if n <= 0 { return 0; }
    c.trail.push(c.depth);
    let a = g(c, n);
    var local = Ctx { depth: 100 + n, trail: c.trail };
    let b = g(local, n - 1);
    return a + b;
}

fn g(c: Ctx&, n: i64) -> i64 { return f(c, n - 1) + 1; }

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 0, trail: trail };
    print(f(c, 3));
    for r in trail { print(r); }
}
