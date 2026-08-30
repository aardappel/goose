// Lifetime roots and provenance through monomorphization (§9.2, §9.5, §10.2):
// per-call-site root identity makes same-root proofs work through parameters.

struct Cell { v: int = 0, link: int&<u16>? }

// Both references derive from the caller's pool: the relative store proves
// same-root through the parameter root classes.
fn store(pool: Cell[>..]&, r: int&) {
    pool.push(Cell { v: 1, link: r });
}

// A returned slice keeps the argument's root at each call site.
fn headtail(xs: int[:]) -> int[:], int {
    return xs[1..], xs[0];
}

// A returned reference into a by-reference parameter maps back to the
// caller's argument root.
fn pick(xs: int[..8]&, i: int) -> int& {
    &xs[i]
}

// The same function body, instantiated for a writable and a non-writable
// slice: only reads here, so both instantiations succeed.
fn total(xs: u8[:]) -> int {
    var n = 0;
    for x in xs { n += x; }
    return n;
}

fn main() {
    var pool: Cell[>..] = [];
    let c0: Cell& = pool.push(Cell { v: 7 });
    store(&pool, &c0.v);
    var data = [1, 2, 3];
    let rest, head = headtail(data);
    print(rest.len + head);
    var lim: int[..8] = [4, 5, 6];
    let r: int& = pick(&lim, 1);   // Annotated: binds the returned reference.
    r = 9;
    print(r + lim[1]);
    let picked = pick(&lim, 2);    // Un-annotated: decays to an int copy.
    print(picked);
    var buf: u8[..4] = [1, 2];
    print(total("bytes") + total(buf[..]));
}
