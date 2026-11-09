// error: assigning to c.a would drop the own Buf? it holds
// What a reference saw emptied was of the struct it pointed at: rebound,
// it knows nothing of the one it points at now.
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
fn main() {
    let h = conn(1);
    let g = conn(2);
    var c: Conn& = h;
    let b = take(c.a);
    c .= g;
    c.a .= b;
    conn_free(h) { drop(take(it.a)); drop(take(it.b)); };
    conn_free(g) { drop(take(it.a)); drop(take(it.b)); };
}
