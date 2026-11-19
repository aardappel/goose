// The same simulation as particles.goose laid out as a struct of arrays: one
// f32 array per component, the shape the C++ SoA row reaches with six
// vectors. The fields are growable rather than fixed-length.
// All the arithmetic is exact in binary32 -- positions and velocities are
// multiples of 1/16 in a small range, the only multiplies are by 0.5 in the
// bounce and 16.0 in the checksum, and both are exact -- so every
// implementation must produce a bit-identical checksum whatever the backend
// does with association or FMA.

import rng;
import vec;

let N = 200000;                          // BENCH_N
let STEPS = 200;

root struct P {
    px: f32[>..] = [],
    py: f32[>..] = [],
    pz: f32[>..] = [],
    vx: f32[>..] = [],
    vy: f32[>..] = [],
    vz: f32[>..] = []
}

fn main() {
    var p: P = P {};
    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        p.px.push(xs_mod(r, 1024));
        p.py.push(xs_mod(r >> 12, 1024));
        p.pz.push(xs_mod(r >> 24, 1024));
        p.vx.push((xs_mod(r >> 36, 256) - 128) / 16.0);
        p.vy.push((xs_mod(r >> 44, 256) - 128) / 16.0);
        p.vz.push((xs_mod(r >> 52, 256) - 128) / 16.0);
    }
    for s in STEPS {
        for i in N {
            p.vy[i] = p.vy[i] - 0.0625;
            p.px[i] = p.px[i] + p.vx[i];
            p.py[i] = p.py[i] + p.vy[i];
            p.pz[i] = p.pz[i] + p.vz[i];
            if p.py[i] < 0.0 {
                p.py[i] = 0.0 - p.py[i];
                p.vy[i] = 0.0 - p.vy[i] * 0.5;
            }
        }
    }
    var total = 0;
    for i in N {
        total += (p.px[i] * 16.0) as i64 + (p.py[i] * 16.0) as i64
               + (p.pz[i] * 16.0) as i64;
    }
    print(N);
    print(total);
}
