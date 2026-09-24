// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// f stores a reference into its context's trail, relying on every call back
// into the cycle passing the context on (§7.8). g, the other member of the
// cycle, passes f a local of its own instead: the error is at f's store,
// naming g's call.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, n: i64) {
    c.trail.push(c.depth);
    if n > 0 { g(c, n - 1); }
}

fn g(c: Ctx&, n: i64) {
    var local = Ctx { depth: 100 + n, trail: c.trail };
    f(local, n);
}

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 0, trail: trail };
    f(c, 2);
    for r in trail { print(r); }
}
