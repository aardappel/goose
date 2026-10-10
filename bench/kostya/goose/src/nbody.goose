// CLBG::Nbody: the Jovian planets orbiting the sun, integrated with a fixed
// time step; the checksum combines the system's energy before and after.

namespace nbody;

import std;
import math;
import bench;
import helper;

let SOLAR_MASS = 4.0 * PI * PI;
let DAYS_PER_YEAR = 365.24;

struct Planet { x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64 }

fn new_planet(x: f64, y: f64, z: f64, vx: f64, vy: f64, vz: f64, mass: f64) -> Planet {
    return Planet {
        x: x,
        y: y,
        z: z,
        vx: vx * DAYS_PER_YEAR,
        vy: vy * DAYS_PER_YEAR,
        vz: vz * DAYS_PER_YEAR,
        mass: mass * SOLAR_MASS,
    };
}

fn advance(p: Planet&, following: Planet[:], dt: f64) {
    for &b2 in following {
        let dx = p.x - b2.x;
        let dy = p.y - b2.y;
        let dz = p.z - b2.z;

        let distance = sqrt(dx * dx + dy * dy + dz * dz);
        let mag = dt / (distance * distance * distance);
        let b_mass_mag = p.mass * mag;
        let b2_mass_mag = b2.mass * mag;

        p.vx -= dx * b2_mass_mag;
        p.vy -= dy * b2_mass_mag;
        p.vz -= dz * b2_mass_mag;
        b2.vx += dx * b_mass_mag;
        b2.vy += dy * b_mass_mag;
        b2.vz += dz * b_mass_mag;
    }

    p.x += dt * p.vx;
    p.y += dt * p.vy;
    p.z += dt * p.vz;
}

fn energy(bodies: Planet[:]) -> f64 {
    var e = 0.0;
    let nbodies = bodies.len;

    for i in nbodies {
        let b .= bodies[i];
        e += 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz);

        for j in i + 1..nbodies {
            let b2 .= bodies[j];
            let dx = b.x - b2.x;
            let dy = b.y - b2.y;
            let dz = b.z - b2.z;
            let distance = sqrt(dx * dx + dy * dy + dz * dz);
            e -= (b.mass * b2.mass) / distance;
        }
    }
    return e;
}

fn offset_momentum(bodies: Planet[:]) {
    var px = 0.0;
    var py = 0.0;
    var pz = 0.0;

    for b in bodies {
        let m = b.mass;
        px += b.vx * m;
        py += b.vy * m;
        pz += b.vz * m;
    }

    let b .= bodies[0];
    b.vx = -px / SOLAR_MASS;
    b.vy = -py / SOLAR_MASS;
    b.vz = -pz / SOLAR_MASS;
}

fn create_bodies() -> Planet[5] {
    return [
        new_planet(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0),
        new_planet(
            4.84143144246472090e+00,
            -1.16032004402742839e+00,
            -1.03622044471123109e-01,
            1.66007664274403694e-03,
            7.69901118419740425e-03,
            -6.90460016972063023e-05,
            9.54791938424326609e-04,
        ),
        new_planet(
            8.34336671824457987e+00,
            4.12479856412430479e+00,
            -4.03523417114321381e-01,
            -2.76742510726862411e-03,
            4.99852801234917238e-03,
            2.30417297573763929e-05,
            2.85885980666130812e-04,
        ),
        new_planet(
            1.28943695621391310e+01,
            -1.51111514016986312e+01,
            -2.23307578892655734e-01,
            2.96460137564761618e-03,
            2.37847173959480950e-03,
            -2.96589568540237556e-05,
            4.36624404335156298e-05,
        ),
        new_planet(
            1.53796971148509165e+01,
            -2.59193146099879641e+01,
            1.79258772950371181e-01,
            2.68067772490389322e-03,
            1.62824170038242295e-03,
            -9.51592254519715870e-05,
            5.15138902046611451e-05,
        ),
    ];
}

fn run(b: Bench&) -> u32 {
    var bodies = create_bodies();
    offset_momentum(bodies);
    let v1 = energy(bodies);
    measure(b) {
        let nbodies = bodies.len;
        let dt = 0.01;
        for n in 1000 {
            for i in nbodies { bodies[i].advance(bodies[i + 1..], dt); }
        }
    };
    let v2 = energy(bodies);
    return (checksum_f64(v1) << 5) & checksum_f64(v2);
}
