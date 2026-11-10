// error: c was consumed at
// A struct holding own values moves: the second read finds nothing.
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
    var c = Conn { fd: 1, scratch: buf_take(1) };
    var d: Conn = c;
    var e: Conn = c;
    buf_drop(take(d.scratch));
    buf_drop(take(e.scratch));
}
