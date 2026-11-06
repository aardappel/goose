// How often an apply evaluates its arguments (spec §11.2). The pool, the
// array chunks() names, its chunk size, offsets or task count, the message
// buffer and each extra argument of a fixed-size type whose value takes
// bytes are evaluated once, in that order, before any task is handed out.
// Every other extra argument is built into each task's packet, so it is
// evaluated once per task, in order, as the task is built, and not at all
// where there is no task: one of a variable-size or resizable type, and one
// of a fixed-size type whose value takes no bytes (a zero-length array, or a
// variant type with no payload), which EmitPoolApply does not hoist. A
// limited array of capacity 0 keeps its length field, and an empty struct
// takes a byte (spec C.2), so both are evaluated once. Each argument notes
// itself in `log` as it is evaluated, on the thread applying the pool, and
// the task of index i gets the (i + 1)th builder. Everything runs on a pool
// of four workers and again on a pool of one; both print the same.

enum Mark { Value { n: i64 }, Dot }

struct Nothing {}

var log:   u8[>..] = [];
var built: i64     = 0;

fn note(what: u8[:]) {
    log.push(' ');
    log.append(what);
}

fn the(p: thread_pool&) -> thread_pool& {
    note("pool");
    return p;
}

fn the_data(d: i64[>..]&) -> i64[>..]& {
    note("array");
    return d;
}

fn size(n: i64) -> i64 {
    note("size");
    return n;
}

fn offsets_of(o: const i64[:]) -> const i64[:] {
    note("offsets");
    return o;
}

fn count(n: i64) -> i64 {
    note("count");
    return n;
}

fn buffer_of(b: u8[>..]&) -> u8[>..]& {
    note("msg");
    return b;
}

fn num(n: i64) -> i64 {
    note("i64");
    return n;
}

fn view(xs: const i64[:]) -> const i64[:] {
    note("slice");
    return xs;
}

fn ref_to(x: const i64&) -> const i64& {
    note("ref");
    return x;
}

fn no_elements() -> i64[0] {
    note("i64[0]");
    return [];
}

fn dot() -> Mark.Dot {
    note("dot");
    return Mark.Dot {};
}

fn no_room() -> i64[..0] {
    note("i64[..0]");
    return [];
}

fn nothing() -> Nothing {
    note("struct");
    return Nothing {};
}

fn bytes() -> u8[] {
    note("u8[]");
    return "ab";
}

// A fresh builder each time, one byte longer than the one before.
fn builder() -> u8[>..] {
    note("u8[>..]");
    built++;
    var b: u8[>..] = [];
    for i in built { b.push('g'); }
    return b;
}

fn fill(
    chunk: i64[:],
    idx:   i64,
    a:     i64,
    s:     const i64[:],
    r:     const i64&,
    z:     i64[0],
    d:     Mark.Dot,
    l:     i64[..0],
    e:     Nothing,
    b:     u8[],
    g:     u8[>..],
) {
    for &x in chunk { x = idx * 100 + a + s.len + r + z.len + l.len + b.len * 10 + g.len; }
}

fn keep(out: i64[>..]&, idx: i64, a: i64, z: i64[0], d: Mark.Dot, g: u8[>..]) {
    out.push(idx * 100 + a + z.len + g.len);
}

fn show(what: u8[:], data: i64[:]) {
    print(what, log);
    print("  ", data);
    log.clear();
    built = 0;
}

fn run_on(pool: thread_pool&) {
    let xs = [5, 6];
    let k = 3;
    var data: i64[>..] = [0, 0, 0, 0, 0, 0];
    pool.apply(chunks(data, size(2)), fill, num(1), view(xs), ref_to(k), no_elements(), dot(),
               no_room(), nothing(), bytes(), builder());
    show("by size:", data);

    var none: i64[>..] = [];
    pool.apply(chunks(none, size(2)), fill, num(1), view(xs), ref_to(k), no_elements(), dot(),
               no_room(), nothing(), bytes(), builder());
    show("no task:", none);

    let offsets = [0, 1, 1, 6];
    pool.apply(chunks(data, offsets_of(offsets)), fill, num(2), view(xs), ref_to(k),
               no_elements(), dot(), no_room(), nothing(), bytes(), builder());
    show("by offsets:", data);

    var out: i64[>..] = [7];
    pool.apply(chunks(out, count(3)), keep, num(1), no_elements(), dot(), builder());
    show("growable:", out);

    var why: u8[>..] = [];
    let ok = the(pool).apply_contained(chunks(the_data(data), size(3)), buffer_of(why), fill,
                                       num(3), view(xs), ref_to(k), no_elements(), dot(),
                                       no_room(), nothing(), bytes(), builder());
    show("contained:", data);
    print("  returned ", ok, ", with a message of ", why.len, " bytes");
}

fn main() {
    var pool = thread_pool(4);
    defer pool.close();
    print("four workers:");
    run_on(pool);
    var solo = thread_pool(1);
    defer solo.close();
    print("one worker:");
    run_on(solo);
}
