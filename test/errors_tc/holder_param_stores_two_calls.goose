// error: cannot clear a while r1 is still used: a reference into it was stored there
// Two calls to the same helper in one caller: the second maps the
// helper's parameter afresh, which must not move the first call's store
// of a view of `a` onto `b` (§5.1).
import std;

struct Row { v: i64[:] }

fn put(rows: Row[>..]&, x: Row) { rows.push(x); }

fn main() {
    var a: i64[>..] = [];
    var b: i64[>..] = [];
    a.push(1);
    b.push(2);
    var r1: Row[>..] = [];
    var r2: Row[>..] = [];
    put(r1, Row { a[..] });
    put(r2, Row { b[..] });
    a.clear();
    print(r1.len + r2.len);
}
