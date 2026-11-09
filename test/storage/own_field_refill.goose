// A field the code has just emptied takes a new value by assignment
// (docs/design/ownership.md 4.6): in a struct variable, through an own
// handle, and through a plain reference, which answers for what was
// emptied through it since anything could have filled it.
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

fn size(b: Buf?) -> i64 {
    if b { return b.n; }
    return 0;
}

// Exchanges a buffer for one twice the size.
fn doubled(b: own Buf?) -> own Buf& {
    let n = size(b) * 2;
    drop(b);
    return buf(if n == 0 { 1 } else { n });
}

fn through_reference(c: Conn&) {
    c.a .= doubled(take(c.a));              // emptied by the take, filled by the store
    c.a .= doubled(take(c.a));
}

fn through_handle(h: own Conn&) -> own Conn& {
    h.b .= doubled(take(h.b));
    return h;
}

fn in_variable() {
    var c = Conn { fd: 3 };
    c.a .= buf(5);                          // the literal left it empty
    c.a .= doubled(take(c.a));
    print("variable ", size(c.a));
    drop(take(c.a));
}

fn main() {
    let h = conn(1);
    through_reference(h);
    print("reference ", size(h.a), " ", size(h.b));
    let h2 = through_handle(h);
    print("handle ", size(h2.a), " ", size(h2.b));
    conn_free(h2) { drop(take(it.a)); drop(take(it.b)); };
    in_variable();
    print("live ", live);
}
