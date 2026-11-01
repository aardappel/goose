// A holder argument read out of `rows` holds views of `k1` or `k2`, where
// they point exactly: the copy `put` stores into `out` holds those, not
// whatever `rows` gets after the call, so clearing `a` is allowed (§5.1).
import std;
struct Row { v: const u8[:] }
fn put(out: Row[>..]&, x: Row) { out.push(x); }
fn main() {
    var k1: u8[>..] = [];
    k1.push('1');
    var k2: u8[>..] = [];
    k2.push('2');
    var out: Row[>..] = [];
    var n = 0;
    if n == 0 {
        var a: u8[>..] = [];
        a.push('a');
        var rows: Row[>..] = [];
        rows.push(Row { k1[..] });
        rows.push(Row { k2[..] });
        put(out, rows[0]);
        rows.push(Row { a[..] });
        a.clear();
        print(out[0].v);
    }
}
