// error: hs[0] holds an own value in storage, which a plain read only borrows
// An element leaves its array by pop.
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
var bufs: Buf[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
fn main() {
    var hs: (own Buf&)[>..<] = [];
    hs.push(buf_take(1));
    buf_release(hs[0]);
}
