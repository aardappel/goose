// An image kernel: a 3x3 Gaussian blur over a WxW 8-bit image, ping-ponged
// between two buffers for a number of passes. Flat scalar data, a stencil
// of nine loads per pixel, and nothing for Goose's memory model to win on:
// this row is here to lose, and to measure by how much. Each access is
// `src[y * W + x]` with x and y loop counters, which is what everyone writes
// first; the bounds-check analysis cannot prove those indices (it reasons
// in differences, not products), so all nine checks per pixel stay, and a
// backend that would have vectorised the C++ loop has to keep them too.
// blur_rows.goose is the same kernel written over row slices, which is the
// form the analysis can prove.

import rng;

let W = 2048;                            // BENCH_N
let PASSES = 16;

fn blur(src: u8[>..]&, dst: u8[>..]&) {
    for y in 1..W - 1 {
        for x in 1..W - 1 {
            let i = y * W + x;
            let s = src[i - W - 1] as u16 + src[i - W] as u16 * 2 + src[i - W + 1] as u16
                  + src[i - 1] as u16 * 2 + src[i] as u16 * 4 + src[i + 1] as u16 * 2
                  + src[i + W - 1] as u16 + src[i + W] as u16 * 2 + src[i + W + 1] as u16;
            dst[i] = (s >> 4) as! u8;
        }
    }
}

fn main() {
    var a: u8[>..] = [];
    var b: u8[>..] = [];
    var r: u64 = 12345;
    for i in W * W {
        r = xs_next(r);
        a.push(xs_mod(r, 256) as! u8);
        b.push(0);
    }
    for p in PASSES / 2 {
        blur(a, b);
        blur(b, a);
    }
    var total = 0;
    for v in a { total += v; }
    print(W);
    print(total);
}
