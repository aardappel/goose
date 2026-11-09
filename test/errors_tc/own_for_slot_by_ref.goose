// error: an own value's slot is not bound by reference
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
var bufs: Buf[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
fn main() {
    var rs: (own u8[:])[>..<] = [];
    for &r in rs { print(r.len); }
}
