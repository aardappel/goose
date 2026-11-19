// The same simulation as particles.rs laid out as a struct of arrays: one
// Vec<f32> per component, the shape the C++ SoA row reaches with six vectors.
// The fields are growable rather than fixed-length, and with no vector struct
// left to run through an operator impl the update is written out per
// component, as in particles_scalar.rs -- the two of them separate what the
// layout costs, the same way the Goose pairs do.
//
// All the arithmetic is exact in binary32, so every row must produce a
// bit-identical checksum however the backend reassociates.
mod bench;
use bench::*;

const N: i64 = 100000;                   // BENCH_N
const STEPS: i64 = 200;

struct P {
    px: Vec<f32>,
    py: Vec<f32>,
    pz: Vec<f32>,
    vx: Vec<f32>,
    vy: Vec<f32>,
    vz: Vec<f32>,
}

fn main() {
    let mut r: u64 = 12345;
    let mut p = P {
        px: Vec::with_capacity(N as usize),
        py: Vec::with_capacity(N as usize),
        pz: Vec::with_capacity(N as usize),
        vx: Vec::with_capacity(N as usize),
        vy: Vec::with_capacity(N as usize),
        vz: Vec::with_capacity(N as usize),
    };
    for _ in 0..N {
        r = xs_next(r);
        p.px.push(xs_mod(r, 1024) as f32);
        p.py.push(xs_mod(r >> 12, 1024) as f32);
        p.pz.push(xs_mod(r >> 24, 1024) as f32);
        p.vx.push((xs_mod(r >> 36, 256) - 128) as f32 / 16.0);
        p.vy.push((xs_mod(r >> 44, 256) - 128) as f32 / 16.0);
        p.vz.push((xs_mod(r >> 52, 256) - 128) as f32 / 16.0);
    }
    for _ in 0..STEPS {
        for i in 0..N as usize {
            p.vy[i] += -0.0625;
            p.px[i] += p.vx[i];
            p.py[i] += p.vy[i];
            p.pz[i] += p.vz[i];
            if p.py[i] < 0.0 { p.py[i] = -p.py[i]; p.vy[i] = -(p.vy[i] * 0.5); }
        }
    }
    let mut total: i64 = 0;
    for i in 0..N as usize {
        total += (p.px[i] * 16.0) as i64 + (p.py[i] * 16.0) as i64 + (p.pz[i] * 16.0) as i64;
    }
    emit(N);
    emit(total);
}
