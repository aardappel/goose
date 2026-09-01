// A float kernel over particles: see bench/goose/particles.goose. `impl Add for
// F3` and then `p.vel = p.vel + G` is the elementwise style, which in Rust means
// deriving `Copy` and writing the operator impl -- more ceremony than Goose's
// built-in elementwise struct math, but the same expression at the use site, and
// it should cost nothing after inlining.
//
// All the arithmetic is exact in binary32, so every row must produce a
// bit-identical checksum however the backend reassociates.
mod bench;
use bench::*;
use std::ops::Add;

const N: i64 = 100000;                   // BENCH_N
const STEPS: i64 = 200;

#[derive(Clone, Copy)]
struct F3 { x: f32, y: f32, z: f32 }

impl Add for F3 {
    type Output = F3;
    fn add(self, o: F3) -> F3 { F3 { x: self.x + o.x, y: self.y + o.y, z: self.z + o.z } }
}

#[derive(Clone, Copy)]
struct P { pos: F3, vel: F3 }

const G: F3 = F3 { x: 0.0, y: -0.0625, z: 0.0 };

fn main() {
    let mut r: u64 = 12345;
    let mut ps: Vec<P> = Vec::with_capacity(N as usize);
    for _ in 0..N {
        r = xs_next(r);
        ps.push(P {
            pos: F3 { x: xs_mod(r, 1024) as f32,
                      y: xs_mod(r >> 12, 1024) as f32,
                      z: xs_mod(r >> 24, 1024) as f32 },
            vel: F3 { x: (xs_mod(r >> 36, 256) - 128) as f32 / 16.0,
                      y: (xs_mod(r >> 44, 256) - 128) as f32 / 16.0,
                      z: (xs_mod(r >> 52, 256) - 128) as f32 / 16.0 },
        });
    }
    for _ in 0..STEPS {
        for p in ps.iter_mut() {
            p.vel = p.vel + G;
            p.pos = p.pos + p.vel;
            if p.pos.y < 0.0 { p.pos.y = -p.pos.y; p.vel.y = -(p.vel.y * 0.5); }
        }
    }
    let mut total: i64 = 0;
    for p in &ps {
        total += (p.pos.x * 16.0) as i64 + (p.pos.y * 16.0) as i64 + (p.pos.z * 16.0) as i64;
    }
    emit(N);
    emit(total);
}
