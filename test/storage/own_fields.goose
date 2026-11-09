// Own values in storage (docs/design/ownership.md 4.6): a field holding one
// is read with take(), which leaves it empty, and written with replace(),
// which gives back what was there; a plain read borrows. A trusted fn that
// ends a handle's claim on a struct checks that the struct's own fields
// were all taken, in the block its caller passes.
struct Buf { n: i64 }
struct Conn { fd: i64, scratch: own Buf?, spare: own Buf? }
struct Inner { a: own u8[:], b: own u8[:] }
struct Outer { tag: i64, inner: Inner, extra: own u8[:] }

var bufs: Buf[>..] = [];
var live = 0;
var conns: Conn[>..] = [];
var conn_live = 0;
var bytes: u8[>..] = [];
var runs = 0;
var current: own Buf? = null;

trusted fn buf_take(n: i64) -> own Buf& { live += 1; return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) { live -= 1; }
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }

trusted fn run_take(n: i64) -> own u8[:] {
    let at = bytes.len;
    for i in n { bytes.push((i + 65) as! u8); }
    runs += 1;
    return bytes[at..at + n];
}
trusted fn run_release(r: own u8[:]) { if r.len > 0 { runs -= 1; } }

// A pool in miniature: the slot's claim ends in conn_release, which the
// checker lets end only where the block emptied every own field.
trusted fn conn_take(c: Conn) -> own Conn& { conn_live += 1; return conns.push(c); }
trusted fn conn_release<F>(h: own Conn&) {
    F(h);
    conn_live -= 1;
}

fn make_conn(fd: i64) -> own Conn& { conn_take(Conn { fd: fd, scratch: buf_take(fd * 10) }) }

fn swap_spare(c: Conn&, n: i64) { buf_drop(replace(c.spare, buf_take(n))); }

fn through_handle() {
    let h = make_conn(1);
    print("conn ", h.fd, " scratch ", h.scratch != null, " live ", live);
    swap_spare(h, 5);
    swap_spare(h, 6);
    let s = h.spare;                   // a borrow: a plain Buf?
    if s { print("spare ", s.n, " live ", live); }
    conn_release(h) {
        buf_drop(take(it.scratch));
        buf_drop(take(it.spare));
    };
    print("released: live ", live, " conns ", conn_live);
}

fn by_value() {
    // A struct variable answers for its own fields itself: the literal left
    // spare empty, and the take leaves scratch so.
    var c = Conn { fd: 7, scratch: buf_take(70) };
    print("local ", c.fd, " ", c.scratch != null, " live ", live);
    buf_drop(take(c.scratch));
    print("taken ", c.scratch == null, " live ", live);
}

fn moved() {
    let c = Conn { fd: 8, scratch: buf_take(80), spare: buf_take(81) };
    let r .= c;
    let h = conn_take(c);              // the move leaves c's own fields empty
    print("moved: source empty ", r.scratch == null, " ", r.spare == null, " pooled ", h.fd);
    conn_release(h) { buf_drop(take(it.scratch)); buf_drop(take(it.spare)); };
}

fn whole(c: Conn&) {
    // take() of a struct moves it out whole and empties what it leaves.
    var d = take(c);
    print("whole ", d.fd, " ", d.scratch != null, " left ", c.scratch == null);
    buf_drop(take(d.scratch));
    buf_drop(take(d.spare));
}

fn nested() {
    var o = Outer { tag: 1, inner: Inner { a: run_take(2), b: run_take(3) }, extra: run_take(4) };
    print("outer ", o.inner.a, " ", o.inner.b.len, " ", o.extra.len, " runs ", runs);
    let r .= o;
    run_release(take(r.inner.a));      // through a borrow of o
    var inner = take(o.inner);
    print("inner ", inner.a.len, " ", inner.b, " left ", o.inner.b.len);
    run_release(take(inner.a));
    run_release(take(inner.b));
    run_release(take(o.extra));
}

fn finish<F>(o: Outer) { F(o); }

fn by_block() {
    let o = Outer { tag: 2, inner: Inner { a: run_take(1), .. }, extra: run_take(1) };
    finish(o) {                        // `it` is the Outer, moved in
        run_release(take(it.inner.a));
        run_release(take(it.inner.b));
        run_release(take(it.extra));
    };
}

fn in_loops() {
    var held = run_take(1);
    for i in 3 {
        let next = run_take(i + 2);
        run_release(replace(held, next));
    }
    print("held ", held.len, " runs ", runs);
    run_release(held);
}

fn global_slot() {
    buf_drop(replace(current, buf_take(1)));
    buf_drop(replace(current, buf_take(2)));
    let c = current;
    if c { print("current ", c.n, " live ", live); }
    buf_drop(take(current));
    print("current empty ", current == null);
}

fn main() {
    through_handle();
    by_value();
    moved();
    let h = make_conn(3);
    whole(h);
    conn_release(h) { buf_drop(take(it.scratch)); buf_drop(take(it.spare)); };
    nested();
    by_block();
    in_loops();
    global_slot();
    print("end: live ", live, " conns ", conn_live, " runs ", runs);
}
