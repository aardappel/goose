// A reference to a fixed-size value passed into a recursive cycle may be
// rooted at another activation's local, so it may be passed down but not
// stored (§7.8); only pool parameters (references to resizable-class values)
// are exempt.
struct Cell { v: i64, link: i64&? }
recursive fn f(pool: Cell[>..]&, r: i64&, n: i64) {
    var local = n;
    pool.push(Cell { v: n, link: r });
    if n > 0 { f(pool, &local, n - 1); }
}
fn main() {
    var p: Cell[>..] = [];
    var x = 5;
    f(p, &x, 3);
    print(p.len);
}
