// CLBG::Nbody: the Jovian planets orbiting the sun, integrated with a fixed
// time step; the checksum combines the system's energy before and after.

namespace nbody;

import std;
import math;
import vec;
import bench;
import helper;

let SOLAR_MASS = 4.0 * PI * PI;
let DAYS_PER_YEAR = 365.24;

struct Planet { pos: double3, vel: double3, mass: f64 }

// A planet from its position, its velocity in units per day, and its mass in
// solar masses.
fn planet(x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64) -> Planet {
    return Planet {
        pos: double3 { x, y, z },
        vel: double3 { vx, vy, vz } * DAYS_PER_YEAR,
        mass: mass * SOLAR_MASS,
    };
}

fn energy(bodies: Planet[:]) -> f64 {
    var e = 0.0;
    for b, i in bodies {
        e += 0.5 * b.mass * length_sq(b.vel);
        for b2 in bodies[i + 1..] {
            e -= (b.mass * b2.mass) / distance(b.pos, b2.pos);
        }
    }
    return e;
}

fn offset_momentum(bodies: Planet[:]) {
    var p = double3_0;
    for b in bodies { p += b.vel * b.mass; }
    // The same value as -p / SOLAR_MASS, which a vector cannot spell.
    bodies[0].vel = p / -SOLAR_MASS;
}

// One time step: every pair exchanges velocity, then every planet moves.
// Each planet is updated in a local copy that is stored back once, so the
// writes to the planets after it cannot touch it and it stays in registers.
fn advance(bodies: Planet[:], dt: f64) {
    for i in bodies.len {
        var b = bodies[i];
        for &b2 in bodies[i + 1..] {
            let d = b.pos - b2.pos;
            let distance = length(d);
            let mag = dt / (distance * distance * distance);
            b.vel -= d * (b2.mass * mag);
            b2.vel += d * (b.mass * mag);
        }
        b.pos += b.vel * dt;
        bodies[i] = b;
    }
}

fn run(b: Bench&) -> u32 {
    var bodies: Planet[5] = [
        planet(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0),
        planet(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
               1.66007664274403694e-03, 7.69901118419740425e-03, -6.90460016972063023e-05,
               9.54791938424326609e-04),
        planet(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
               -2.76742510726862411e-03, 4.99852801234917238e-03, 2.30417297573763929e-05,
               2.85885980666130812e-04),
        planet(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
               2.96460137564761618e-03, 2.37847173959480950e-03, -2.96589568540237556e-05,
               4.36624404335156298e-05),
        planet(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
               2.68067772490389322e-03, 1.62824170038242295e-03, -9.51592254519715870e-05,
               5.15138902046611451e-05),
    ];
    offset_momentum(bodies);
    let v1 = energy(bodies);
    measure(b) {
        for n in 1000 { advance(bodies, 0.01); }
    };
    let v2 = energy(bodies);
    return (checksum_f64(v1) << 5) & checksum_f64(v2);
}
