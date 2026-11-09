// The pools' owning operations (docs/design/ownership.md): slot_pool's take
// and release, slice_pool's take_slice, resize and release, and a pool
// whose elements own runs of another, released with a block the checker
// holds to taking every own field.
import slot_pool;
import slice_pool;

struct Node { key: i64, next: own Node? }
struct Conn { fd: i64, inbuf: own u8[:], scratch: own u8[:] }

var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
// A pool whose elements own slots of the same pool never shrinks while any
// may be linked (reference §5.1), so it is a global: a local one could not
// end empty.
var nodes = slot_pool<Node> {};

fn live<T>(p: slot_pool<T>&) -> i64 { p.items.len - p.free_slots.len }

fn slots() {
    var pool = slot_pool<i64> {};
    let a = pool.take(10);
    let b = pool.take(20);
    a += 1;                            // a handle reads and writes as the reference it is
    print("slots ", a, " ", b, " live ", live(pool), " at ", pool.items.index_of(b));
    pool.release(a);
    let c = pool.take(30);             // the slot a held
    print("reused ", pool.items.index_of(c), " ", c, " ", pool.items);
    pool.release(b);
    pool.release(c);
    print("live ", live(pool));
}

// A list whose nodes own their successors: releasing the head releases
// what it owns first, each node's claim ending exactly once.
fn list() {
    var head: own Node? = null;
    for k in 4 { head .= nodes.take(Node { key: k, next: take(head) }); }
    var n = 0;
    while head {
        n += head.key;
        var rest: own Node? = null;
        nodes.release(head) { rest .= take(it.next); };   // the block must take what the node owns
        head .= rest;
    }
    print("list sum ", n, " live ", live(nodes));
}

// A local pool of own-holding elements ends empty: release_all hands every
// slot's element to the block, a released one owning nothing.
fn local_pool() {
    var pool = slot_pool<Conn> {};
    let a = pool.take(Conn { fd: 7, inbuf: bytes.take_slice(2), scratch: [] });
    let b = pool.take(Conn { fd: 8, inbuf: [], scratch: bytes.take_slice(3) });
    print("local ", a.fd + b.fd, " bytes ", bytes.items.len);
    pool.release(a) {
        bytes.release(take(it.inbuf));
        bytes.release(take(it.scratch));
    };
    pool.release(b) {
        bytes.release(take(it.inbuf));
        bytes.release(take(it.scratch));
    };
    pool.release_all() {
        bytes.release(take(it.inbuf));
        bytes.release(take(it.scratch));
    };
    print("emptied ", pool.items.len);
}

fn runs() {
    var pool = slice_pool<i64> {};
    let a = pool.take_slice(3);
    a[0] = 1; a[1] = 2; a[2] = 3;
    let b = pool.take_copy([7, 8]);
    print("runs ", a, " ", b, " ", pool.items.len);
    let a2 = pool.resize(a, 5);        // the old run is consumed, the new one returned
    print("resized ", a2, " ", pool.items.len);
    pool.release(a2);
    pool.release(b);
    let c = pool.take_slice(0);        // an empty run owns nothing; releasing it is harmless
    pool.release(c);
    var d: own i64[:] = [];            // and a variable written as one owes nothing until it is filled
    if pool.items.len > 5 { d = pool.take_copy([4, 5, 6]); }
    print("late ", d);
    pool.release(take(d));
}

fn open(fd: i64) -> own Conn& {
    conns.take(Conn { fd: fd, inbuf: bytes.take_slice(8), scratch: bytes.take_slice(4) })
}

fn close(c: own Conn&) {
    conns.release(c) {
        bytes.release(take(it.inbuf));
        bytes.release(take(it.scratch));
    };
}

fn grow(c: Conn&, n: i64) {
    // take() empties the field, and the run resize returns fills it again.
    c.scratch = bytes.resize(take(c.scratch), n);
    // replace() exchanges a run for another, here an empty one for itself.
    bytes.release(replace(c.inbuf, replace(c.inbuf, [])));
}

fn elements() {
    let a = open(1);
    let b = open(2);
    a.inbuf[0] = 65;
    grow(a, 16);
    print("conns ", live(conns), " a ", a.inbuf.len, "/", a.scratch.len, " b ", b.scratch.len,
          " bytes ", bytes.items.len);
    close(a);
    let c = open(3);
    print("reused ", conns.items.index_of(c), " scratch ", c.scratch.len);
    close(b);
    close(c);
    print("conns ", live(conns));
}

fn main() {
    slots();
    list();
    local_pool();
    runs();
    elements();
}
