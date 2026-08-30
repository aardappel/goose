// Control benchmark: a flat array of scalars, scanned repeatedly. Nothing here
// plays to a Goose strength -- the data is fixed-size, contiguous in every
// language, and the loop is the same machine code. A dead heat here is the
// evidence that the harness measures the algorithm and not the toolchain.

import rng;

let N = 4000000;                         // BENCH_N
let PASSES = 8;

fn main() {
    var data: i32[>..] = [];
    var r = 12345;
    for i in N {
        r = xs_next(r);
        data.push(xs_mod(r, 1000) as i32);
    }
    var total = 0;
    for p in PASSES {
        // The `^ p` keeps the passes from collapsing into one multiply.
        for x in data { total += x ^ p; }
    }
    print(data.len);
    print(total);
}
