// Image kernels over row slices. A grayscale image is generated, blurred,
// and run through a Sobel edge detector, and the result is written as a
// PGM file. The kernels take the three source rows and the destination row
// as slices whose lengths are asserted once per row, so inside the pixel
// loop every index is a loop counter plus a constant against a known
// length: the compiler proves the pixel loops' bounds checks away and the
// C backend sees the same loop it would see from C.
//
// Build and run (from this directory):
//   goose -o image_filters.c 22_image_filters.goose && cl image_filters.c && image_filters

import std;
import math;
import os;

let W = 512;
let H = 384;

// A test picture: a gradient with rings, a bright disc and some noise.
fn generate(img: u8[>..]&) {
    var r = rng { 99 };
    for y in H {
        for x in W {
            let dx = (x - W / 2) as f64;
            let dy = (y - H / 2) as f64;
            let d = sqrt(dx * dx + dy * dy);
            var v = 96.0 + 64.0 * sin(d / 12.0) + (x as f64) / W as f64 * 60.0;
            if d < 60.0 { v = 230.0; }
            if (x / 64 + y / 64) % 2 == 0 && y > H * 3 / 4 { v = 40.0; }
            v += (r.rand_int(21) - 10) as f64;
            img.push(clamp(v, 0.0, 255.0) as u8);
        }
    }
}

// A 3x3 box blur of the interior; the border is copied. The rows are
// sliced once per output row, and the inner loop indexes those.
fn blur(src: u8[:], dst: u8[:]) {
    assert(src.len == W * H && dst.len == W * H);
    copy_into(dst[..W], src[..W]);
    copy_into(dst[(H - 1) * W..], src[(H - 1) * W..]);
    for y in 1..H - 1 {
        let a = src[(y - 1) * W..y * W];
        let b = src[y * W..(y + 1) * W];
        let c = src[(y + 1) * W..(y + 2) * W];
        let d = dst[y * W..(y + 1) * W];
        assert(a.len == W && b.len == W && c.len == W && d.len == W);
        d[0] = b[0];
        d[W - 1] = b[W - 1];
        for x in 1..W - 1 {
            let s = a[x - 1] as u16 + a[x] as u16 + a[x + 1] as u16
                  + b[x - 1] as u16 + b[x] as u16 + b[x + 1] as u16
                  + c[x - 1] as u16 + c[x] as u16 + c[x + 1] as u16;
            d[x] = (s / 9) as! u8;
        }
    }
}

// Sobel edge magnitude, same shape: two 3x3 kernels, one per axis.
fn sobel(src: u8[:], dst: u8[:]) {
    assert(src.len == W * H && dst.len == W * H);
    fill(dst[..W], 0);
    fill(dst[(H - 1) * W..], 0);
    for y in 1..H - 1 {
        let a = src[(y - 1) * W..y * W];
        let b = src[y * W..(y + 1) * W];
        let c = src[(y + 1) * W..(y + 2) * W];
        let d = dst[y * W..(y + 1) * W];
        assert(a.len == W && b.len == W && c.len == W && d.len == W);
        d[0] = 0;
        d[W - 1] = 0;
        for x in 1..W - 1 {
            let gx = (a[x + 1] as i32 - a[x - 1] as i32) + 2 * (b[x + 1] as i32 - b[x - 1] as i32)
                   + (c[x + 1] as i32 - c[x - 1] as i32);
            let gy = (c[x - 1] as i32 - a[x - 1] as i32) + 2 * (c[x] as i32 - a[x] as i32)
                   + (c[x + 1] as i32 - a[x + 1] as i32);
            let g = abs(gx) + abs(gy);
            d[x] = if g > 255 { 255 } else { g as! u8 };
        }
    }
}

fn histogram(img: u8[:]) -> i64[8] {
    var h: i64[8] = [0; 8];
    for v in img { h[v / 32] += 1; }
    return h;
}

fn write_pgm(path: u8[:], img: u8[:]) {
    var out: u8[>..] = [];
    format(out, "P5 ", W, " ", H, " 255\n");
    out.append(img);
    print(path, ": ", write_file(path, out), " (", out.len, " bytes)");
}

fn main() {
    var img: u8[>..] = [];
    generate(img);
    var tmp: u8[>..] = [];
    tmp.resize(W * H, 0);
    var edges: u8[>..] = [];
    edges.resize(W * H, 0);

    let t0 = clock();
    for i in 4 {                         // four blur passes, ping-ponging the buffers
        blur(img, tmp);
        blur(tmp, img);
    }
    let t1 = clock();
    sobel(img, edges);
    let t2 = clock();

    var note: u8[>..] = [];
    format(note, "8 blur passes: ");
    format_flt(note, (t1 - t0) * 1000.0, 2);
    format(note, " ms, sobel: ");
    format_flt(note, (t2 - t1) * 1000.0, 2);
    format(note, " ms\n");
    write_stderr(note);

    print("histogram of the blurred image: ", histogram(img));
    print("histogram of the edges: ", histogram(edges));
    var strong = 0;
    for v in edges { if v > 128 { strong++; } }
    print(strong, " strong edge pixels of ", W * H);
    write_pgm("blurred.pgm", img);
    write_pgm("edges.pgm", edges);
}
