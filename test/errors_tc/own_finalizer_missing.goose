// error: trusted fn conn_free ends the claim of h on a Conn whose own fields scratch, spare may still hold a value
// A release with no block leaves what the element owns unaccounted for.
struct Buf { n: i64 }
struct Conn { fd: i64, scratch: own Buf?, spare: own Buf? }
var bufs: Buf[>..] = [];
var conns: Conn[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
trusted fn conn_take(c: Conn) -> own Conn& { return conns.push(c); }
trusted fn conn_release<F>(h: own Conn&) { F(h); }
trusted fn conn_free(h: own Conn&) {}
fn main() {
    let h = conn_take(Conn { fd: 1, scratch: buf_take(1) });
    conn_free(h);
}
