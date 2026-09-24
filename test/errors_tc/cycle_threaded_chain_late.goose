// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// f's call back passes a local of its own for c before f first calls g with
// c, so g's context is whatever each activation of f was given (§7.8), and
// g passing it back on does not make it storable: g's store is the error,
// naming f's call.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, t: (i64&)[>..]&, n: i64) {
    if n <= 0 { return; }
    var local = Ctx { depth: 100 + n, trail: t };
    f(local, t, n - 1);
    g(c, t, n);
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
