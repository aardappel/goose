// error: assigning to c.b would drop the own Buf? it holds
// Emptying one field says nothing of another.
struct Buf { n: i64 }
struct Conn { fd: i64, a: own Buf?, b: own Buf? }

var bufs: Buf[>..] = [];
var conns: Conn[>..] = [];
var live = 0;
trusted fn buf(n: i64) -> own Buf& { live += 1; return bufs.push(Buf { n: n }); }
trusted fn buf_free(b: own Buf&) { live -= 1; }
fn drop(b: own Buf?) { if b { buf_free(b); } }
trusted fn conn(fd: i64) -> own Conn& { return conns.push(Conn { fd: fd }); }
trusted fn conn_free<F>(h: own Conn&) { F(h); }
fn swap_in(c: Conn&, nb: own Buf&) {
    drop(take(c.a));
    c.b .= nb;
}
fn main() {}
