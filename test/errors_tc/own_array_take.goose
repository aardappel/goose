// error: take moves one own value, and ts is a Task[>..<]
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
var bufs: Buf[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
fn main() {
    var ts: Task[>..<] = [];
    ts.push(Task { id: 1, buf: buf_take(1) });
    var us = take(ts);
}
