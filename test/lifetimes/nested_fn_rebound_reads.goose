// A nested function, or a function a function value is handed to, is
// checked for what it finds of the variables outside it that it names: a
// call finding one pointing, or holding references, elsewhere, or assigned
// where it was not, is checked apart (§7.5). Each program here is valid at
// every call, and each specialization runs.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
var spare: Cell[>..] = [];
struct K { r: Cell&?, n: i64 }
var keep: K[>..] = [];
struct L { r: Cell&<u32 in cells>?, n: i64 }
var links: L[>..] = [];

fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn apply<F>() { F(); }

fn show() {
    for k in keep {
        let r = k.r;
        if r { print(k.n, ": ", r.v); } else { print(k.n, ": null"); }
    }
    keep.clear();
}

// v gains spare's element beside cells' between the calls.
fn rebound() {
    var v: Cell&? = pick(true, cells[0]);
    fn put(n: i64 = 3) { keep.push(K { r: v, n: n }); }
    put(1);
    v .= pick(true, spare[0]);
    put(2);
    apply(put);
}

// A link within cells, then null, which gives v no other root: both calls
// find v as the first one did, and share its check.
fn links_then_null() {
    var v: Cell&? = pick(true, cells[1]);
    fn link(n: i64) { links.push(L { r: v, n: n }); }
    link(1);
    v .= null;
    link(2);
    for l in links {
        let r = l.r;
        if r { print("link ", l.n, ": ", r.v); } else { print("link ", l.n, ": null"); }
    }
}

// The holder's contents grow from cells to spare; relay reads it through put.
fn holder() {
    var h = K { r: cells[1], n: 3 };
    fn put() { keep.push(h); }
    fn relay() { put(); }
    relay();
    h = K { r: spare[0], n: 4 };
    relay();
    put();
}

// n is assigned on both branches before rd's second call.
fn assigned(c: bool) {
    var n: i64;
    fn rd() { print("n = ", n); }
    if c {
        n = 1;
        rd();
    } else {
        n = 2;
    }
    rd();
}

// The block reads v in every pass of the loop; the rebind at the end of the
// body gives it spare's element from the second iteration on.
fn looped() {
    var v: Cell&? = pick(true, cells[0]);
    for i in 0..3 {
        apply() { keep.push(K { r: v, n: 10 + i }); };
        v .= pick(true, spare[0]);
    }
}

fn main() {
    cells.push(Cell { v: 5 });
    cells.push(Cell { v: 7 });
    spare.push(Cell { v: 6 });
    rebound();
    show();
    links_then_null();
    holder();
    show();
    assigned(true);
    assigned(false);
    looped();
    show();
}
