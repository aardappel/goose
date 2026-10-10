// CLBG::Spectralnorm: power iteration towards the spectral norm of the
// infinite matrix A[i][j] = 1 / ((i + j)(i + j + 1) / 2 + i + 1), cut to
// size x size.

namespace spectralnorm;

import std;
import math;
import bench;
import helper;

fn eval_a(i: i64, j: i64) -> f64 {
    let ij = (i + j) as f64;
    return 1.0 / (ij * (ij + 1.0) / 2.0 + i + 1.0);
}

// out = A u
fn eval_a_times_u(u: f64[:], out: f64[:]) {
    for i in out.len {
        var sum = 0.0;
        for x, j in u { sum += eval_a(i, j) * x; }
        out[i] = sum;
    }
}

// out = A^T u
fn eval_at_times_u(u: f64[:], out: f64[:]) {
    for i in out.len {
        var sum = 0.0;
        for x, j in u { sum += eval_a(j, i) * x; }
        out[i] = sum;
    }
}

// out = A^T A u, through a fresh intermediate vector.
fn eval_ata_times_u(u: f64[:], out: f64[:]) {
    var tmp: f64[>..] = [];
    tmp.resize(u.len, 0.0);
    eval_a_times_u(u, tmp);
    eval_at_times_u(tmp, out);
}

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "size");
    var u: f64[>..] = [];
    var v: f64[>..] = [];
    u.resize(n, 1.0);
    v.resize(n, 1.0);
    measure(b) {
        eval_ata_times_u(u, v);
        eval_ata_times_u(v, u);
    };
    var vbv = 0.0;
    var vv = 0.0;
    for i in n {
        let vi = v[i];
        vbv += u[i] * vi;
        vv += vi * vi;
    }
    return checksum_f64(sqrt(vbv / vv));
}
