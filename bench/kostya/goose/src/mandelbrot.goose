// CLBG::Mandelbrot: the Mandelbrot set as a 1-bit PBM image, appended to one
// growing buffer per run; the checksum is taken over the whole buffer.

namespace mandelbrot;

import std;
import bench;
import helper;

let ITER = 50;
let LIMIT = 2.0;

struct Mandelbrot { w: i64, h: i64, result_bin: u8[>..] }

fn new_mandelbrot() -> Mandelbrot {
    Mandelbrot {
        w: config_i64("CLBG::Mandelbrot", "w"),
        h: config_i64("CLBG::Mandelbrot", "h"),
        result_bin: [],
    }
}

fn prepare(s: Mandelbrot&) {}

fn run(s: Mandelbrot&, iteration_id: i64) {
    let w = s.w;
    let h = s.h;

    format(s.result_bin, "P4\n", w, " ", h, "\n");

    var bit_num = 0;
    var byte_acc: u8 = 0;

    for y in h {
        let ci = 2.0 * y / h - 1.0;
        for x in w {
            var zr = 0.0;
            var zi = 0.0;
            var tr = 0.0;
            var ti = 0.0;
            let cr = 2.0 * x / w - 1.5;
            var i = 0;
            while i < ITER && tr + ti <= LIMIT * LIMIT {
                zi = 2.0 * zr * zi + ci;
                zr = tr - ti + cr;
                tr = zr * zr;
                ti = zi * zi;
                i++;
            }
            byte_acc <<= 1;
            if tr + ti <= LIMIT * LIMIT { byte_acc |= 0x01; }
            bit_num++;
            if bit_num == 8 {
                s.result_bin.push(byte_acc);
                byte_acc = 0;
                bit_num = 0;
            } else if x == w - 1 {
                byte_acc <<= 8 - w % 8;
                s.result_bin.push(byte_acc);
                byte_acc = 0;
                bit_num = 0;
            }
        }
    }
}

fn checksum(s: Mandelbrot&) -> u32 { checksum_str(s.result_bin) }
