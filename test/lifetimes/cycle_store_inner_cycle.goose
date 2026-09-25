// f's cycle calls h, which stores the reference it is given into its own
// local and then calls count. h is in neither cycle: it never calls back into
// f's, and count's starts at count. So its store is an ordinary one, not one
// inside a cycle (§7.8).
struct Cell { v: i64, link: Cell&? }

recursive fn count(c: Cell&, n: i64) -> i64 {
    if n <= 0 { return c.v; }
    return count(c, n - 1) + 1;
}

fn h(c: Cell&, n: i64) -> i64 {
    var mine = Cell { v: n, link: null };
    mine.link .= c;
    var linked = 0;
    let l = mine.link;
    if l { linked = l.v; }
    return count(mine, n) + linked;
}

recursive fn f(p: Cell&, n: i64) -> i64 {
    var local = Cell { v: 10 * n + p.v, link: null };
    let got = h(local, n);
    if n > 0 { return got + f(local, n - 1); }
    return got;
}

fn main() {
    var a = Cell { v: 1, link: null };
    print(f(a, 3));
}
