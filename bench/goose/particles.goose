// A float kernel over an array-of-structs, written the idiomatic way: the
// update is elementwise struct math (spec 6.1), so `p.vel = p.vel + G` adds
// all three components even though gravity only acts on one. particles_scalar
// .goose is the same simulation with the components written out, and the C++
// rows come in both shapes too, so the four of them separate what the style
// costs from what the language costs.
//
// All the arithmetic is exact in binary32 -- positions are integers, velocities
// and gravity are multiples of 1/16, and the only multiply is by 0.5 -- so
// every implementation must produce a bit-identical checksum whatever the
// backend does with association or FMA.

import rng;
import vec;

let N = 200000;                          // BENCH_N
let STEPS = 200;

struct P { pos: float3, vel: float3 }

let G = float3 { 0.0, -0.0625, 0.0 };

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
        ps.push(P { pos: float3 { px, py, pz }, vel: float3 { vx, vy, vz } });
    }
    for s in STEPS {
        for &p in ps {
            p.vel = p.vel + G;
            p.pos = p.pos + p.vel;
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
