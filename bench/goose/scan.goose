// Collecting the positions of every byte in a set: a scalar loop for the
// baseline row, a single find_all for the batched row. Both print the same
// checksums; what differs is how the positions get into the array.

import rng;
import std;

let N = 2000000;                          // BENCH_N

fn main() {
    // A text of N lines of 30-89 letters: about one set byte in sixty, the
    // moderate density where a batched scan wins over a byte loop. (Dense
    // separators leave the output write, not the scan, as the cost.)
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
    for pass in 8 {
        pos.resize(0);
        for c, i in text { if c == '\n' { pos.push(i); } }
        npos = pos.len;
        for p in pos { total += p; }
    }
    print(npos);
    print(total);
}
