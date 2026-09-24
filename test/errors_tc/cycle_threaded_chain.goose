// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// g's context is f's, which g passes back on, and g's store relies on that
// (§7.8). Each activation of f calls g with the context it was given,
// though, and f's own call back passes a local: g's store is the error,
// naming f's call.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, t: (i64&)[>..]&, n: i64) {
    if n <= 0 { return; }
    g(c, t, n);
    var local = Ctx { depth: 100 + n, trail: t };
    f(local, t, n - 1);
}

fn g(c: Ctx&, t: (i64&)[>..]&, n: i64) {
    f(c, t, n - 1);
    c.trail.push(c.depth);
}

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 0, trail: trail };
    f(c, trail, 2);
    for r in trail { print(r); }
}
