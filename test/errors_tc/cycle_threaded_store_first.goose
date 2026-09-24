// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes c rooted differently from the cycle's entry call
// A reference rooted at a context every call back into the cycle passes on
// may be stored inside the cycle (§7.8), and walk's first statement relies on
// that. Its call back then passes a local of the activation instead, a
// reference to which the next activation would store into main's trail,
// which the local does not outlive: the error is at the store, naming the
// call.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn walk(c: Ctx&, n: i64) {
    c.trail.push(c.depth);
    if n > 0 {
        var inner = Ctx { depth: c.depth + 1, trail: c.trail };
        walk(inner, n - 1);
    }
}

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 0, trail: trail };
    walk(c, 2);
    for r in trail { print(r); }
}
