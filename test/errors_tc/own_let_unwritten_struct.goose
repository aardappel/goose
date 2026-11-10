// error: var d = c does not say what becomes of the own value c holds
// error: write var d: Conn = c to move it or var d .= c to borrow it
// A struct holding own values is moved by a declaration that writes its
// type, and is never copied.
struct Buf { n: i64 }
struct Conn { fd: i64, scratch: own Buf? }
var bufs: Buf[>..] = [];
trusted fn buf_take(n: i64) -> own Buf& { return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) {}
fn main() {
    var c = Conn { fd: 1, scratch: buf_take(1) };
    var d = c;
    let b = take(d.scratch);
    if b { buf_release(b); }
}
