// The same simulation as particles.goose with the vector update written out
// component by component, which is the shape the C++ AoS row has. The two
// Goose rows differ only in expression style, so the gap between them is what
// elementwise struct math (spec 6.1) costs -- it adds gravity's zero x and z
// components that the written-out form skips.
//
// All the arithmetic is exact in binary32 -- positions are integers, velocities
// and gravity are multiples of 1/16, and the only multiply is by 0.5 -- so
// every implementation must produce a bit-identical checksum whatever the
// backend does with association or FMA.

import rng;

let N = 200000;                          // BENCH_N
let STEPS = 200;

struct F3 { x: f32, y: f32, z: f32 }
struct P { pos: F3, vel: F3 }

fn main() {
    var ps: P[>..] = [];
    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        let px = xs_mod(r, 1024) as f32;
        let py = xs_mod(r >> 12, 1024) as f32;
        let pz = xs_mod(r >> 24, 1024) as f32;
        let vx = (xs_mod(r >> 36, 256) - 128) as f32 / 16.0;
        let vy = (xs_mod(r >> 44, 256) - 128) as f32 / 16.0;
        let vz = (xs_mod(r >> 52, 256) - 128) as f32 / 16.0;
        ps.push(P { pos: F3 { x: px, y: py, z: pz },
                    vel: F3 { x: vx, y: vy, z: vz } });
    }
    for s in STEPS {
        for &p in ps {
            p.vel.y = p.vel.y - 0.0625;
            p.pos.x = p.pos.x + p.vel.x;
            p.pos.y = p.pos.y + p.vel.y;
            p.pos.z = p.pos.z + p.vel.z;
            if p.pos.y < 0.0 {
                p.pos.y = 0.0 - p.pos.y;
                p.vel.y = 0.0 - p.vel.y * 0.5;
            }
        }
    }
    var total = 0;
    for &p in ps {
        total += (p.pos.x * 16.0) as i64 + (p.pos.y * 16.0) as i64
               + (p.pos.z * 16.0) as i64;
    }
    print(ps.len);
    print(total);
}
