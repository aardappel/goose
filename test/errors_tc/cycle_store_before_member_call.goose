// error: references may only be passed down, not stored, inside a recursive cycle
// error: h calls into its recursive cycle at
// h reaches f's cycle only through down, whose check has finished by then:
// the call still leads back into the cycle, so h is in it, and so is the
// store it made before that call (§7.8).
struct Cell { v: i64, link: Cell&? }

recursive fn f(p: Cell&, q: Cell&, n: i64) {
    if n <= 0 { return; }
    down(q, n);
    h(p, q, n);
}

fn down(q: Cell&, n: i64) {
    var local = Cell { v: 100 + n, link: null };
    f(local, q, n - 1);
}

fn h(d: Cell&, e: Cell&, n: i64) {
    e.link .= d;
    down(e, n);
}

fn main() {
    var a = Cell { v: 1, link: null };
    var b = Cell { v: 2, link: null };
    f(a, b, 2);
    let l = b.link;
    if l { print(l.v); }
}
