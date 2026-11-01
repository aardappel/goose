// error: cannot clear other while r is still used
// A recursive function returns its holder parameter after a nested function
// stored a view of `other` into it: the result holds what the parameter
// was given only where nothing else was stored into it, in any round of the
// cycle's check, whose later rounds may reuse the nested function's (§5.1).
import std;
struct Row { v: const u8[:] }
recursive fn pass(x: Row, s: const u8[:], n: i64) -> Row {
    fn g() { x.v = s; }
    g();
    if n > 0 { let y = pass(Row { "" }, s, n - 1); print(y.v.len); }
    return x;
}
fn main() {
    var other: u8[>..] = [];
    other.push('o');
    let r = pass(Row { "" }, other[..], 1);
    other.clear();
    print(r.v);
}
