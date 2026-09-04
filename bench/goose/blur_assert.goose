// The flat kernel of blur.goose with the image size stated once per call.
// `blur` takes its buffers as fat references, and before the analysis carried
// a caller's length facts into a callee this assert -- two comparisons
// against four million pixels -- was the only way the kernel could compare
// `y * W + x` against anything. Today the same facts arrive from the call
// sites and the assert is redundant, so this row is the control: it must
// time the same as blur.goose, and it shows what an explicit precondition
// costs when the compiler already knows it.

import rng;

let W = 2048;                            // BENCH_N
let PASSES = 16;

fn blur(src: u8[>..]&, dst: u8[>..]&) {
    assert(src.len == W * W && dst.len == W * W);
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
