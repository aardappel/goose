// error: copy of a Task[>..<], which holds an own value
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
var bufs: Buf[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }
fn main() {
    var ts: Task[>..<] = [];
    ts.push(Task { id: 1, buf: buf_take(1) });
    var us = copy(ts);
    while ts.len > 0 { var t = ts.pop(); buf_drop(take(t.buf)); }
}
