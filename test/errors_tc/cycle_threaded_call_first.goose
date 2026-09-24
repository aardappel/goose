// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// The call back into the cycle swaps the two contexts before any store has
// relied on either being passed on, which leaves both pass-down-only
// (§7.8): the store after it is the error, naming the call. The second
// activation would store a reference into b, which main's inner block ends
// before trail.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn walk(c: Ctx&, d: Ctx&, n: i64) {
    if n > 0 { walk(d, c, n - 1); }
    c.trail.push(c.depth);
}

fn main() {
    var trail: (i64&)[>..] = [];
    var a = Ctx { depth: 1, trail: trail };
    {
        var b = Ctx { depth: 2, trail: trail };
        walk(a, b, 2);
    }
    for r in trail { print(r); }
}
