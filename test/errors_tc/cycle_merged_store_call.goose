// error: references may only be passed down, not stored, inside a recursive cycle
// What a call returns of its parameter's pointee may be whatever the argument
// may (§9.2): id(pick) is rooted at the pool, which a cycle may store into
// (§7.8), but may be `x`, which a back edge passes a local of its own
// activation for.
struct Cell { v: i64, prev: Cell&? }
fn id(c: Cell&) -> Cell& { return c; }

recursive fn f(pool: Cell[>..]&, x: Cell&, n: i64) -> i64 {
    var loc = Cell { v: 7 + n, prev: null };
    let pick: Cell& = if n % 2 == 1 { pool[0] } else { x };
    pool.push(Cell { v: n, prev: id(pick) });
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
