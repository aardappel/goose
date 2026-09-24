// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// f's store relies on its context being passed on around the cycle (§7.8).
// g passes f its own context back, which its first call rooted where f's is,
// but g's own call back passes g a local for it: from then on g's context
// is whatever each activation of g was given, and so is what g gives f.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, t: (i64&)[>..]&, n: i64) {
    c.trail.push(c.depth);
    if n > 0 { g(c, t, n - 1); }
}

recursive fn g(c: Ctx&, t: (i64&)[>..]&, n: i64) {
    if n > 5 {
        var local = Ctx { depth: 100 + n, trail: t };
        g(local, t, n - 1);
    }
    f(c, t, n - 1);
}

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 0, trail: trail };
    f(c, trail, 8);
    for r in trail { print(r); }
}
