// The flat kernel of blur.goose with the image size stated once per call.
// `blur` takes its buffers as fat references, and a reference parameter's
// length is unknown inside the callee, so the analysis has nothing to compare
// `y * W + x` against however well it reasons about the product itself. One
// assert per call -- two comparisons against four million pixels -- supplies
// the missing half, and all ten checks then disappear. This row measures what
// the flat form costs when only the length fact is added, against blur.goose
// (nothing elided) and blur_rows.goose (the same win through row slices).

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
