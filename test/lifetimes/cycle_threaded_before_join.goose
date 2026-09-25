// g is not recursive, but its call back into f makes it part of f's cycle
// from its first statement on, the store before that call included. The
// store keeps a reference read through the context c, which may be stored
// inside the cycle while every call back into it passes c on as main gave
// it (§7.8): from g's call back on the store relies on that, and the call
// does pass c on.
struct Ctx { depth: i64, trail: (i64&)[>..]& }

recursive fn f(c: Ctx&, n: i64) {
    if n > 0 { g(c, n); }
}

fn g(c: Ctx&, n: i64) {
    c.trail.push(c.depth);
    f(c, n - 1);
}

fn main() {
    var trail: (i64&)[>..] = [];
    var c = Ctx { depth: 7, trail: trail };
    f(c, 3);
    for r in trail { print(r); }
}
