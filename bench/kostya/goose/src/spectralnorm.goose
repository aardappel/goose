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

// A u
fn eval_a_times_u(u: f64[:]) -> f64[>..] {
    let n = u.len;
    var result: f64[>..] = [];
    result.resize(n, 0.0);
    for &r, i in result {
        var sum = 0.0;
        for uu, j in u { sum += eval_a(i, j) * uu; }
        r = sum;
    }
    return result;
}

// A^T u
fn eval_at_times_u(u: f64[:]) -> f64[>..] {
    let n = u.len;
    var result: f64[>..] = [];
    result.resize(n, 0.0);
    for &r, i in result {
        var sum = 0.0;
        for uu, j in u { sum += eval_a(j, i) * uu; }
        r = sum;
    }
    return result;
}

// A^T A u
fn eval_ata_times_u(u: f64[:]) -> f64[>..] {
    let temp = eval_a_times_u(u);
    return eval_at_times_u(temp);
}

fn run(b: Bench&) -> u32 {
    let size_val = config_i64(b.name, "size");
    var u: f64[>..] = [];
    var v: f64[>..] = [];
    u.resize(size_val, 1.0);
    v.resize(size_val, 1.0);
    measure(b) {
        v = eval_ata_times_u(u);
        u = eval_ata_times_u(v);
    };
    var vbv = 0.0;
    var vv = 0.0;
    for i in size_val {
        let vi = v[i];
        vbv += u[i] * vi;
        vv += vi * vi;
    }
    let result_value = sqrt(vbv / vv);
    return checksum_f64(result_value);
}
