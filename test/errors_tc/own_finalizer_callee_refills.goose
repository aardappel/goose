// error: trusted fn conn_release ends the claim of h on a Conn
// A callee that may replace an own value does the same to what the caller knew.
struct Buf { n: i64 }
struct Conn { fd: i64, scratch: own Buf?, spare: own Buf? }
var bufs: Buf[>..] = [];
var conns: Conn[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
trusted fn conn_take(c: Conn) -> own Conn& { return conns.push(c); }
trusted fn conn_release<F>(h: own Conn&) { F(h); }
fn refill(c: Conn&) { buf_drop(replace(c.spare, buf_take(3))); }
fn main() {
    let h = conn_take(Conn { fd: 1, scratch: buf_take(1) });
    conn_release(h) {
        buf_drop(take(it.scratch));
        buf_drop(take(it.spare));
        refill(it);
    };
}
