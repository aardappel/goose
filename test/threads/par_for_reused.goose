// goose-threads
// A function checked for one par for and called again from another
// (docs/design/child_threads.md §4.6): what its check found holds for the
// second loop too. Here the generic run is handed bump, which a reduction
// of total makes, so each loop keeps per-chunk totals of its own and adds
// exactly n; the helper scale, which reads a global, is reused beside
// the parts of a local array, which no global is; and shift, whose calls
// all pass two arrays apart, each named exactly, keeps writing one while
// reading the other, whichever way around they come, as add_view does
// through a reference to a slice of another array.
let FACTOR = 3;

// A global slice no function can bind again views the one array it is
// given, which is no parameter's (kernel_view).
const SEED: i64[4] = [1, 2, 3, 4];

let VIEW: const i64[:] = SEED;

fn kernel_view(dst: i64[:]) { par for k in dst.len { dst[k] = VIEW[(k + 1) % 4] + 1; } }

fn run<F>() { F(); }

fn twice<F>() { F(); F(); }

fn scale(k: i64) -> i64 { return FACTOR * k; }

fn shift(dst: i64[:], src: const i64[:]) {
    par for k in dst.len { dst[k] = src[(k + 1) % dst.len] + 1; }
}

fn shift_on(x: i64[:], y: i64[:]) { shift(x, y); }

// The slice behind a reference to a slice slot is a view of its own, apart
// from the other parameter where every call passes another array.
fn add_view(xs: i64[:], r: i64[:]&) { par for k in xs.len { xs[k] += r[k % r.len]; } }

fn main() {
    var total = 0;
    fn bump() { total++; }
    let n = 100000;
    par for k in n { run(bump); }
    print(total);
    par for k in n { run(bump); }
    print(total);
    par for k in n { twice(bump); }
    print(total);
    var out: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
    par for k in 8 { out[k] = scale(k); }
    par for k in 8 { out[k] += scale(1); }
    print(out);
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    var b: i64[8] = [10, 20, 30, 40, 50, 60, 70, 80];
    shift(a, b);
    shift(b, a);
    shift_on(a, b);
    shift_on(b, a);
    print(a, " ", b);
    var w = b[0..4];
    add_view(a, w);
    print(a);
    var kv: i64[4] = [0, 0, 0, 0];
    kernel_view(kv);
    kernel_view(kv);
    print(kv);
}
