// Lifetime roots and provenance through monomorphization (§9.2, §9.5, §10.2):
// per-call-site root identity makes same-root proofs work through parameters.

struct Cell { v: i64 = 0, link: i64&<u16>? }

// Both references derive from the caller's pool: the relative store proves
// same-root through the parameter root classes.
fn store(pool: Cell[>..]&, r: i64&) {
    pool.push(Cell { v: 1, link: r });
}

// A returned slice keeps the argument's root at each call site.
fn headtail(xs: i64[:]) -> i64[:], i64 {
    return xs[1..], xs[0];
}

// A returned reference into a by-reference parameter maps back to the
// caller's argument root.
fn pick_rt(xs: i64[..8]&, i: i64) -> i64& {
    xs[i]
}

// The same function body, instantiated for a writable and a non-writable
// slice: only reads here, so both instantiations succeed.
fn total(xs: u8[:]) -> i64 {
    var n = 0;
    for x in xs { n += x; }
    return n;
}

fn main_roots() {
    var pool: Cell[>..] = [];
    let c0: Cell& = pool.push(Cell { v: 7 });
    store(pool, c0.v);
    var data = [1, 2, 3];
    let rest, head = headtail(data);
    print(rest.len + head);
    var lim: i64[..8] = [4, 5, 6];
    let r: i64& = pick_rt(lim, 1);   // Annotated: binds the returned reference.
    r = 9;
    print(r + lim[1]);
    let picked = pick_rt(lim, 2);    // Un-annotated: decays to an i64 copy.
    print(picked);
    var buf: u8[..4] = [1, 2];
    print(total("bytes") + total(buf[..]));
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_roots(); }
