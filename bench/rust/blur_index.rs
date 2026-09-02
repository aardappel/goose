// An image kernel: a 3x3 Gaussian blur over a WxW 8-bit image, ping-ponged
// between two buffers for sixteen passes. See bench/goose/blur.goose. Flat
// scalar data and nothing for a memory model to win on -- this is the control at
// the other end of the suite from the tree benchmarks.
//
// Each access is `src[y * w + x]` with x and y loop counters, which is what
// everyone writes first. Rust cannot prove those indices in bounds any more than
// Goose can (the reasoning would have to be about a product, not a difference),
// so the checks stay in the hot loop: the emitted kernel still branches to
// `panic_bounds_check` on each of the nine loads and the store, and vectorises
// only partly around them. blur_windows.rs is the same kernel written the way a
// Rust programmer rewrites it once that shows up in a profile.
mod bench;
use bench::*;

const W: i64 = 1024;                     // BENCH_N
const PASSES: i64 = 16;

fn blur(src: &[u8], dst: &mut [u8], w: usize) {
    for y in 1..w - 1 {
        for x in 1..w - 1 {
            let i = y * w + x;
            let s = src[i - w - 1] as u16 + src[i - w] as u16 * 2 + src[i - w + 1] as u16
                  + src[i - 1] as u16 * 2 + src[i] as u16 * 4 + src[i + 1] as u16 * 2
                  + src[i + w - 1] as u16 + src[i + w] as u16 * 2 + src[i + w + 1] as u16;
            dst[i] = (s >> 4) as u8;
        }
    }
}

fn main() {
    let w = W as usize;
    let mut a: Vec<u8> = Vec::new();
    let mut b: Vec<u8> = Vec::new();
    let mut r: u64 = 12345;
    for _ in 0..W * W {
        r = xs_next(r);
        a.push(xs_mod(r, 256) as u8);
        b.push(0);
    }
    // Border pixels are never written, so b's border stays zero throughout.
    for _ in 0..PASSES / 2 {
        blur(&a, &mut b, w);
        blur(&b, &mut a, w);
    }
    let mut total: i64 = 0;
    for v in &a { total += *v as i64; }
    emit(W);
    emit(total);
}
