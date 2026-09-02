// The same blur written over row slices, which is how a Rust programmer gets rid
// of the bounds checks without writing `unsafe`: slice the three source rows and
// the interior of the destination row once per row, then walk them with
// `windows(3)` zipped against the destination iterator. Every index inside the
// inner loop is then into a slice whose length the compiler knows -- three, from
// `windows`, and the zip stops at the shortest -- so nothing is checked per
// pixel and the loop the backend sees is the one C++ writes directly. See
// bench/goose/blur_rows.goose, which is the same rewrite in Goose.
//
// The cost is that the shape of the kernel is now expressed as an iterator
// pipeline rather than as nine offsets from a centre pixel, which is the usual
// trade for this idiom.
mod bench;
use bench::*;

const W: i64 = 1024;                     // BENCH_N
const PASSES: i64 = 16;

fn blur(src: &[u8], dst: &mut [u8], w: usize) {
    for y in 1..w - 1 {
        let a = &src[(y - 1) * w..y * w];
        let b = &src[y * w..(y + 1) * w];
        let c = &src[(y + 1) * w..(y + 2) * w];
        let d = &mut dst[y * w + 1..y * w + w - 1];
        for (((wa, wb), wc), o) in a.windows(3).zip(b.windows(3)).zip(c.windows(3)).zip(d) {
            let s = wa[0] as u16 + wa[1] as u16 * 2 + wa[2] as u16
                  + wb[0] as u16 * 2 + wb[1] as u16 * 4 + wb[2] as u16 * 2
                  + wc[0] as u16 + wc[1] as u16 * 2 + wc[2] as u16;
            *o = (s >> 4) as u8;
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
