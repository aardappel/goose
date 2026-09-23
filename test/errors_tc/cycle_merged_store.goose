// error: references may only be passed down, not stored, inside a recursive cycle
// Inside a recursive cycle only a reference into a pool handed to it, a
// global or static data may be stored (§7.8). A branch value that may be
// either an element of the pool or `x` is rooted at the pool, the deeper of
// the two, but may still be `x`, which a back edge passes a local of its
// own activation for.
struct Cell { v: i64, prev: Cell&? }

recursive fn f(pool: Cell[>..]&, x: Cell&, n: i64) -> i64 {
    var loc = Cell { v: 7 + n, prev: null };
    let pick: Cell& = if n % 2 == 1 { pool[0] } else { x };
    pool.push(Cell { v: n, prev: pick });
    if n > 0 { f(pool, loc, n - 1); }
    return 0;
}

fn main() {
    var cell = Cell { v: 1, prev: null };
    {
        var pool: Cell[>..] = [];
        pool.push(Cell { v: 100, prev: null });
        f(pool, cell, 2);
        print(pool.len);
    }
}
