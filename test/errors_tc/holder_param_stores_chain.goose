// error: cannot clear other while rows is still used
// put_copy stores a copy of its own holder, reached through mid; mid's
// first call, from second, does not leave the stores mapped for first
// (§5.1).
import std;
struct Row { v: i64[:] }
fn put_copy(rows: Row[>..]&, x: Row) {
    var z = Row { x.v };
    rows.push(z);
}
fn mid(rows: Row[>..]&, x: Row) { put_copy(rows, x); }
fn second() {
    var keep: i64[>..] = [];
    keep.push(2);
    var rows: Row[>..] = [];
    mid(rows, Row { keep[..] });
    print(rows.len);
}
fn first() {
    var other: i64[>..] = [];
    other.push(1);
    var rows: Row[>..] = [];
    mid(rows, Row { other[..] });
    other.clear();
    print(rows[0].v[0]);
}
fn main() { second(); first(); }
