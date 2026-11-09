// error: trusted fn conn_release ends the claim of h on a Conn whose own fields scratch, spare may still hold a value
// A replace may fill what a take emptied, so nothing is known empty after it.
struct Buf { n: i64 }
struct Conn { fd: i64, scratch: own Buf?, spare: own Buf? }
var bufs: Buf[>..] = [];
var conns: Conn[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
trusted fn conn_take(c: Conn) -> own Conn& { return conns.push(c); }
trusted fn conn_release<F>(h: own Conn&) { F(h); }
fn main() {
    let h = conn_take(Conn { fd: 1, scratch: buf_take(1) });
    conn_release(h) {
        buf_drop(take(it.scratch));
        buf_drop(take(it.spare));
        buf_drop(replace(it.scratch, buf_take(2)));
    };
}
