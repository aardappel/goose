// Matmul::Single, T4, T8, T16: the product of two n x n matrices, with the
// second one transposed first so both operands of every dot product are rows.

namespace matmul;

import std;
import bench;
import helper;

// A matrix is one flat array holding its rows back to back, and row(m, n, i)
// is its row i as a slice. The threaded variants send each worker its own
// copy of a and b, and a value crossing to another thread can hold no slices
// or references, so a matrix can't be an array of row slices.
fn row(m: f64[:], n: i64, i: i64) -> f64[:] { m[i * n..(i + 1) * n] }

fn matgen(n: i64) -> f64[>..] {
    let tmp = 1.0 / (n * n) as f64;
    var a: f64[>..] = [];
    for i in n {
        for j in n { a.push(tmp * (i - j) * (i + j)); }
    }
    return a;
}

// Columns [first, last) of the n x n matrix b as the rows of a new matrix:
// all of b transposed for 0..n.
fn transpose(b: f64[:], n: i64, first: i64, last: i64) -> f64[>..] {
    var b_t: f64[>..] = [];
    b_t.resize((last - first) * n, 0.0);
    for i in n {
        for j in first..last {
            row(b_t, n, j - first)[i] = row(b, n, i)[j];
        }
    }
    return b_t;
}

fn matmul_sequential(a: f64[:], b: f64[:], n: i64) -> f64[>..] {
    let b_t = transpose(b, n, 0, n);

    var c: f64[>..] = [];
    c.resize(n * n, 0.0);

    for i in n {
        let a_row = row(a, n, i);
        let c_row = row(c, n, i);

        for j in n {
            let b_col = row(b_t, n, j);
            var s = 0.0;

            for k in n {
                s += a_row[k] * b_col[k];
            }

            c_row[j] = s;
        }
    }

    return c;
}

fn single(bench: Bench&) -> u32 {
    let n = config_i64(bench.name, "n");
    let a = matgen(n);
    let b = matgen(n);
    var result_val: u32 = 0;
    measure(bench) {
        let c = matmul_sequential(a, b, n);
        let center_value = row(c, n, n >> 1)[n >> 1];
        result_val += checksum_f64(center_value);
    };
    return result_val;
}

// --- the threaded variants ----------------------------------------------------

// Goose threads share no memory, so each worker holds its own copy of a and
// b, received once when the pool starts, as the other languages' benchmark
// objects hold theirs. Each run splits the columns of c evenly over the
// workers: a worker transposes just its columns of b, which are the rows of
// b_t its dot products read, so that together they transpose all of b, and
// it sends its columns of c back for matmul_parallel to assemble.

struct Job { first: i64, last: i64 }                       // first < 0: stop
struct Columns { first: i64, last: i64, cells: f64[] }     // row i: c[i][first..last]

thread_fn worker(a: f64[], b: f64[], n: i64) {
    loop {
        let job = qget<Job>();
        guard job.first >= 0 else { break; }

        let b_t = transpose(b, n, job.first, job.last);

        let width = job.last - job.first;
        var c: f64[>..] = [];
        c.resize(n * width, 0.0);

        for i in n {
            let a_row = row(a, n, i);
            let c_row = row(c, width, i);

            for j in width {
                let b_col = row(b_t, n, j);
                var s = 0.0;

                for k in n {
                    s += a_row[k] * b_col[k];
                }

                c_row[j] = s;
            }
        }

        qput(Columns { first: job.first, last: job.last, cells: copy(c) });
    }
}

fn matmul_parallel(n: i64, threads: i64) -> f64[>..] {
    let columns_per_worker = (n + threads - 1) / threads;
    for worker_id in threads {
        let first = min(worker_id * columns_per_worker, n);
        qput(Job { first: first, last: min(first + columns_per_worker, n) });
    }

    var c: f64[>..] = [];
    c.resize(n * n, 0.0);

    for t in threads {
        let part = qget<Columns>();
        let width = part.last - part.first;
        for i in n {
            copy_into(row(c, n, i)[part.first..part.last], row(part.cells, width, i));
        }
    }

    return c;
}

fn parallel(bench: Bench&, threads: i64) -> u32 {
    let n = config_i64(bench.name, "n");
    let a = matgen(n);
    let b = matgen(n);
    var workers: i64[>..] = [];
    for t in threads { workers.push(thread_spawn(worker, copy(a), copy(b), n)); }
    var result_val: u32 = 0;
    measure(bench) {
        let c = matmul_parallel(n, threads);
        let center_value = row(c, n, n >> 1)[n >> 1];
        result_val += checksum_f64(center_value);
    };
    for t in threads { qput(Job { first: -1, last: -1 }); }
    for id in workers { thread_wait(id); }
    return result_val;
}
