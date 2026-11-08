// error: cannot clear other while rows is still used
// A nested function stores its holder parameter into the parent's rows,
// called by a function that hands it a view of `other` (§5.1): the store
// into a lexical parent's variable holds what the argument held, at every
// level the call goes through.
import std;
struct Row { v: const u8[:] }
fn each<F>(x: const u8[:]) { F(Row { x }); }
fn main() {
    var other: u8[>..] = [];
    other.push('o');
    var rows: Row[>..] = [];
    fn put(r: Row) { rows.push(r); }
    each(other[..], put);
    other.clear();
    print(rows[0].v);
}
