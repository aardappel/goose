// Arrays whose elements hold own values (docs/design/ownership.md 4.6): an
// element enters by push and leaves by pop, each a move; reads borrow, and
// an own field of an element is exchanged with take() and replace() where
// it lies. A local one must be empty where its scope ends.
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
root struct Sched { tasks: Task[>..<] = [], done: i64 = 0 }

var bufs: Buf[>..] = [];
var live = 0;
var table: Task[>..] = [];            // a global never exits

trusted fn buf_take(n: i64) -> own Buf& { live += 1; return bufs.push(Buf { n: n }); }
trusted fn buf_release(b: own Buf&) { live -= 1; }
fn buf_drop(b: own Buf?) { if b { buf_release(b); } }

// The bulk removal the library writes: each element is moved out to the
// block, which the checker holds to consuming all of it.
fn drain<T, F>(xs: T[>..<]&) { while xs.len > 0 { F(xs.pop()); } }

fn handles() {
    var hs: (own Buf&)[>..<] = [];
    for i in 3 { hs.push(buf_take(i)); }
    var sum = 0;
    for h in hs { sum += h.n; }        // each h borrows: a plain Buf&
    let first .= hs.push(buf_take(9));
    print("handles ", hs.len, " sum ", sum, " first ", hs[0].n, " pushed ", first.n, " live ", live);
    while hs.len > 0 { buf_release(hs.pop()); }
    print("popped, live ", live);
}

fn tasks() {
    var ts: Task[>..<] = [];
    for i in 3 { ts.push(Task { id: i, buf: buf_take(i * 10) }); }
    ts.push(Task { id: 9 });
    for &t in ts {
        let b = t.buf;
        if b { print("task ", t.id, " buf ", b.n); } else { print("task ", t.id, " no buf"); }
    }
    buf_drop(replace(ts[1].buf, buf_take(77)));
    buf_drop(take(ts[0].buf));
    var last = ts.pop();               // the element moves out whole
    print("last ", last.id, " live ", live);
    buf_drop(take(last.buf));
    drain(ts) { buf_drop(take(it.buf)); };
    print("drained ", ts.len, " live ", live);
}

fn limited() {
    var small: Task[..4] = [];
    small.push(Task { id: 1, buf: buf_take(5) });
    var t = small.pop();
    buf_drop(take(t.buf));
    print("limited ", small.len, " live ", live);
}

fn optionals() {
    var slots: (own Buf?)[>..<] = [];
    slots.push(buf_take(1));
    slots.push(null);
    buf_drop(take(slots[0]));
    while slots.len > 0 { buf_drop(slots.pop()); }
    print("optionals ", slots.len, " live ", live);
}

fn roots() {
    var s = Sched { .. };
    s.tasks.push(Task { id: 1, buf: buf_take(1) });
    s.tasks.push(Task { id: 2 });
    while s.tasks.len > 0 {
        var t = s.tasks.pop();
        buf_drop(take(t.buf));
        s.done += 1;
    }
    print("sched done ", s.done, " live ", live);
}

fn globals() {
    table.push(Task { id: 1, buf: buf_take(4) });
    table.push(Task { id: 2, buf: buf_take(5) });
    for &t in table { buf_drop(take(t.buf)); }
    print("table ", table.len, " live ", live);
}

fn main() {
    handles();
    tasks();
    limited();
    optionals();
    roots();
    globals();
}
