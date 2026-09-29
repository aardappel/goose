// error: storing a reference into p, which holds a grow-shrink array
// stash's parameter names the field of an element of rows itself (§3.8,
// §4.1), which the clear frees and the push reuses: a reference into a
// grow-shrink array's elements is never stored (§5.2).
struct Row { s: u8[:] }
struct K { p: u8[:]& }
var buf: u8[>..] = [];
var rows: Row[>..<] = [];
var keep: K[>..] = [];
fn stash(p: u8[:]&) { keep.push(K { p: p }); }
fn main() {
    buf.append("abzz");
    rows.push(Row { s: buf[0..2] });
    stash(rows[0].s);
    rows.clear();
    rows.push(Row { s: buf[2..4] });
    print(keep[0].p);
}
