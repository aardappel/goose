// A function no call reaches is still checked, as if the global initializers
// called it with a global of its own for each reference, slice or holder
// parameter: the arrays behind them are arrays the body may shrink, apart
// from each other, and their references may be stored wherever a global's
// may, inside a recursive cycle too. Nothing here calls any of these.
struct Box { r: i64& }
struct Held { xs: i64[>..]& }
struct Row { xs: i64[] }
var keep: Box[>..] = [];
var names: (const u8[:])[>..] = [];

fn drop_all(a: i64[>..]&) { a.clear(); }
fn drop_held(h: Held) { let xs .= h.xs; xs.clear(); }
fn drop_other(a: i64[>..]&, b: i64[>..]&) { let r = &b[0]; a.clear(); print(r); }
fn refill(a: i64[>..]&, src: const i64[:]) { a.clear(); for x in src { a.push(x); } }
recursive fn drain(a: i64[>..]&, n: i64) { if n > 0 { a.pop(); drain(a, n - 1); } }
fn remember(r: i64&) { keep.push(Box { r: r }); }
fn register(n: const u8[:]) { names.push(n); }
recursive fn remember_all(r: i64&, n: i64) {
    if n > 0 { keep.push(Box { r: r }); remember_all(r, n - 1); }
}
fn counted(b: i64[>..]&) -> i64[] { b.push(1); return [1, 2]; }
fn add_row(rows: Row[>..]&, b: i64[>..]&) { rows.push(Row { xs: counted(b) }); }

fn main() { print(keep.len, " ", names.len); }
