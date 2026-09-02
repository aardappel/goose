// The blur of blur.goose written over row slices: three source rows and
// one destination row are sliced once per row, their lengths asserted to
// be W, and the inner loop indexes those. Every index is then a loop
// counter plus a constant against a length the analysis knows, so the
// bounds checks disappear from the hot loop and the backend sees the same
// loop C++ does. The gap between this row and blur.goose is what the checks
// were costing.

import rng;

let W = 2048;                            // BENCH_N
let PASSES = 16;

fn blur(src: u8[>..]&, dst: u8[>..]&) {
    for y in 1..W - 1 {
        let lo = (y - 1) * W;
        let a = src[lo..lo + W];
        let b = src[lo + W..lo + 2 * W];
        let c = src[lo + 2 * W..lo + 3 * W];
        let d = dst[lo + W..lo + 2 * W];
        assert(a.len == W && b.len == W && c.len == W && d.len == W);
        for x in 1..W - 1 {
            let s = a[x - 1] as u16 + a[x] as u16 * 2 + a[x + 1] as u16
                  + b[x - 1] as u16 * 2 + b[x] as u16 * 4 + b[x + 1] as u16 * 2
                  + c[x - 1] as u16 + c[x] as u16 * 2 + c[x + 1] as u16;
            d[x] = (s >> 4) as! u8;
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
        blur(&a, &b);
        blur(&b, &a);
    }
    var total = 0;
    for v in a { total += v; }
    print(W);
    print(total);
}
