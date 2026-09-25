// error: references may only be passed down, not stored, inside a recursive cycle
// error: passes p rooted differently from the cycle's entry call
// g is not recursive, but its call back into f makes it part of f's cycle
// from its first statement on: the store before that call is as much inside
// the cycle as one after it, and relies on d's class being passed on around
// the cycle (§7.8). The call back passes f's local instead, which the next
// activation hands g as d: b would end up linked to a dead local of f, so the
// store is the error, naming the call.
struct Cell { v: i64, link: Cell&? }

recursive fn f(p: Cell&, q: Cell&, n: i64) {
    var local = Cell { v: 100 + n, link: null };
    g(p, q, local, n);
}

fn g(d: Cell&, e: Cell&, c: Cell&, n: i64) {
    e.link .= d;
    if n > 0 { f(c, e, n - 1); }
}

fn main() {
    var a = Cell { v: 1, link: null };
    var b = Cell { v: 2, link: null };
    f(a, b, 2);
    let l = b.link;
    if l { print(l.v); }
}
