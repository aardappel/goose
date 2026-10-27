// error: references may only be passed down, not stored, inside a recursive cycle
// A reference parameter of a recursive cycle may be rooted at another
// activation's local, so it may be passed down but not stored (§7.8), unless
// it is a pool parameter (a reference to a resizable-class value) or the
// entry call roots it outside the cycle and every call back into the cycle
// passes it on. The recursive call here passes `&local` instead, so the store
// of r is the error, and it names that call.
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
