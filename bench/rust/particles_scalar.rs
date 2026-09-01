// The same simulation with the update written out per component, which is what
// a Rust programmer writes when they have not bothered with an operator impl.
// Paired with particles.rs so the two separate what the style costs from what
// the language costs, the same way the Goose and C++ pairs do.
mod bench;
use bench::*;

const N: i64 = 100000;                   // BENCH_N
const STEPS: i64 = 200;

#[derive(Clone, Copy)]
struct F3 { x: f32, y: f32, z: f32 }

#[derive(Clone, Copy)]
struct P { pos: F3, vel: F3 }

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
            p.vel.y += -0.0625;
            p.pos.x += p.vel.x;
            p.pos.y += p.vel.y;
            p.pos.z += p.vel.z;
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
