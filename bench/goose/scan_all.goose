// scan.goose with the collection batched: one find_all writes every hit
// where the scalar row runs a byte loop.

import rng;
import std;

let N = 2000000;                          // BENCH_N

fn main() {
    var text: u8[>..] = [];
    var r:    u64     = 12345;
    for i in N {
        r = xs_next(r);
        let n = 30 + xs_mod(r, 60);
        for j in n {
            r = xs_next(r);
            text.push(('a' + xs_mod(r, 26)) as! u8);
        }
        text.push('\n');
    }
    var total: i64 = 0;
    var npos = 0;
    var pos: i64[>..] = [];
    push_n(pos, 0, N);
    let nl = byte_set("\n");
    for pass in 8 {
        npos = find_all(text, nl, pos[..]);
        for p in pos[..npos] { total += p; }
    }
    print(npos);
    print(total);
}
