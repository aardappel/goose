// error: assigning to h.a would drop the own Buf? it holds
// A call that may store an own value, made between the take and the
// assignment, may have filled the field again.
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
fn refill(c: Conn&) { drop(replace(c.a, buf(9))); }
fn main() {
    let h = conn(1);
    let b = take(h.a);
    refill(h);
    h.a .= b;
    conn_free(h) { drop(take(it.a)); drop(take(it.b)); };
}
