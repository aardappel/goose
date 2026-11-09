// A handle kept in a field and taken out again is no longer provably its
// pool's to the checker, so slot_pool's release checks it as it runs
// (index_of's form with a message): one given to another pool aborts.
import slot_pool;

struct Buf { n: i64 }
struct Conn { fd: i64, buf: own Buf? }

var a = slot_pool<Buf> {};
var b = slot_pool<Buf> {};
var conns = slot_pool<Conn> {};

fn close(c: own Conn&, into: slot_pool<Buf>&) {
    conns.release(c) {
        let h = take(it.buf);
        if h { into.release(h); }
    };
}

fn main() {
    b.release(b.take(Buf { n: 0 }));
    let c = conns.take(Conn { fd: 1, buf: a.take(Buf { n: 2 }) });
    let d = conns.take(Conn { fd: 2, buf: a.take(Buf { n: 3 }) });
    close(c, a);
    print("released ", a.free_slots.len);
    close(d, b);
    print("not reached");
}
