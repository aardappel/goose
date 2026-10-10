// CLBG::Mandelbrot: the Mandelbrot set as a 1-bit PBM image, appended to one
// growing buffer per run; the checksum is taken over the whole buffer.

namespace mandelbrot;

import std;
import bench;
import helper;

let ITER = 50;
let LIMIT = 2.0;

fn run(b: Bench&) -> u32 {
    let w = config_i64(b.name, "w");
    let h = config_i64(b.name, "h");
    var result_bin: u8[>..] = [];
    measure(b) {
        format(result_bin, "P4\n", w, " ", h, "\n");
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
                    result_bin.push(byte_acc);
                    byte_acc = 0;
                    bit_num = 0;
                } else if x == w - 1 {
                    byte_acc <<= 8 - w % 8;
                    result_bin.push(byte_acc);
                    byte_acc = 0;
                    bit_num = 0;
                }
            }
        }
    };
    return checksum_str(result_bin);
}
