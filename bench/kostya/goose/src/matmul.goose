// Matmul::Single, T4, T8, T16: the product of two n x n matrices, with the
// second one transposed first so both operands of every dot product are rows.

namespace matmul;

import std;
import bench;
import helper;

// A matrix is an array of rows. An array cannot hold growable arrays, so the
// rows are runs of a slice pool.
fn new_matrix(pool: f64[>..]&, rows: i64, cols: i64) -> (f64[:])[>..] {
    var m: (f64[:])[>..] = [];
    for i in rows { m.push(pool.alloc_slice(cols)); }
    return m;
}

fn matgen(pool: f64[>..]&, n: i64) -> (f64[:])[>..] {
    let tmp = 1.0 / (n * n) as f64;
    var a = new_matrix(pool, n, n);
    for i in n {
        for j in n { a[i][j] = tmp * (i - j) * (i + j); }
    }
    return a;
}

// Columns [first, last) of the n x n matrix b as the rows of a new matrix:
// all of b transposed for 0..n.
fn transpose(pool: f64[>..]&, b: (f64[:])[:], first: i64, last: i64) -> (f64[:])[>..] {
    let n = b.len;
    var b_t = new_matrix(pool, last - first, n);
    for i in n {
        for j in first..last {
            b_t[j - first][i] = b[i][j];
        }
    }
    return b_t;
}

fn matmul_sequential(pool: f64[>..]&, a: (f64[:])[:], b: (f64[:])[:]) -> (f64[:])[>..] {
    let n = a.len;
    let b_t = transpose(pool, b, 0, n);

    var c = new_matrix(pool, n, n);

    for i in n {
        let a_row = a[i];
        let c_row = c[i];

        for j in n {
            let b_col = b_t[j];
            var s = 0.0;

            for k in n {
                s += a_row[k] * b_col[k];
            }

            c_row[j] = s;
        }
    }

    return c;
}

// --- the threaded variants ----------------------------------------------------

// Goose threads share no memory, so each worker holds its own copy of a and
// b, received once when the pool starts, as the other languages' benchmark
// objects hold theirs. Each run splits the columns of c evenly over the
// workers: a worker transposes just its columns of b, which are the rows of
// b_t its dot products read, so that together they transpose all of b, and
// it sends its columns of c back for matmul_parallel to assemble.
//
// A value crossing to another thread holds no slices, so a matrix travels as
// its rows back to back in one array, and the receiver makes rows over its
// copy.

struct Job { first: i64, last: i64 }                       // first < 0: stop
struct Columns { first: i64, last: i64, cells: f64[] }     // row i: c[i][first..last]

fn flatten(m: (f64[:])[:]) -> f64[>..] {
    var cells: f64[>..] = [];
    for row in m { cells.append(row); }
    return cells;
}

fn rows_of(cells: f64[:], rows: i64, cols: i64) -> (f64[:])[>..] {
    var m: (f64[:])[>..] = [];
    for i in rows { m.push(cells[i * cols..(i + 1) * cols]); }
    return m;
}

thread_fn worker(a_cells: f64[], b_cells: f64[], n: i64) {
    let a = rows_of(a_cells, n, n);
    let b = rows_of(b_cells, n, n);

    loop {
        let job = qget<Job>();
        guard job.first >= 0 else { break; }

        reusable[] var pool: f64[>..] = [];
        let b_t = transpose(pool, b, job.first, job.last);

        let width = job.last - job.first;
        var c = new_matrix(pool, n, width);

        for i in n {
            let a_row = a[i];
            let c_row = c[i];

            for j in width {
                let b_col = b_t[j];
                var s = 0.0;

                for k in n {
                    s += a_row[k] * b_col[k];
                }

                c_row[j] = s;
            }
        }

        qput(Columns { first: job.first, last: job.last, cells: flatten(c) });
    }
}

fn start_workers(a: (f64[:])[:], b: (f64[:])[:], num_threads: i64) -> i64[>..] {
    var workers: i64[>..] = [];
    for t in num_threads { workers.push(thread_spawn(worker, flatten(a), flatten(b), a.len)); }
    return workers;
}

fn matmul_parallel(pool: f64[>..]&, n: i64, num_threads: i64) -> (f64[:])[>..] {
    let columns_per_worker = (n + num_threads - 1) / num_threads;
    for worker_id in num_threads {
        let first = min(worker_id * columns_per_worker, n);
        qput(Job { first: first, last: min(first + columns_per_worker, n) });
    }

    var c = new_matrix(pool, n, n);

    for t in num_threads {
        let part = qget<Columns>();
        let part_rows = rows_of(part.cells, n, part.last - part.first);
        for i in n {
            copy_into(c[i][part.first..part.last], part_rows[i]);
        }
    }

    return c;
}

// --- the benchmarks -----------------------------------------------------------

// The matrices' rows are runs of a slice pool the benchmark owns. The
// threaded variants' workers start when it is made, with their copies of a
// and b.
struct BaseMatmul { n: i64, a: (f64[:])[], b: (f64[:])[], result_val: u32, workers: i64[] }

fn new_base(pool: f64[>..]&, name: u8[:], num_threads: i64) -> BaseMatmul {
    let n = config_i64(name, "n");
    let a = matgen(pool, n);
    let b = matgen(pool, n);
    return BaseMatmul { n: n, a: a, b: b, result_val: 0, workers: start_workers(a, b, num_threads) };
}

// The checksum, which follows the last run, stops the workers, so that the
// next threaded benchmark's workers have the job queue to themselves.
fn stop_workers(base: BaseMatmul&) {
    for id in base.workers { qput(Job { first: -1, last: -1 }); }
    for id in base.workers { thread_wait(id); }
}

struct Matmul1T { base: BaseMatmul }

fn new_matmul1t(pool: f64[>..]&) -> Matmul1T { Matmul1T { base: new_base(pool, "Matmul::Single", 0) } }

fn prepare(s: Matmul1T&) {}

fn run(s: Matmul1T&, iteration_id: i64) {
    reusable[] var pool: f64[>..] = [];
    let c = matmul_sequential(pool, s.base.a, s.base.b);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul1T&) -> u32 { s.base.result_val }

struct Matmul4T { base: BaseMatmul }

fn new_matmul4t(pool: f64[>..]&) -> Matmul4T { Matmul4T { base: new_base(pool, "Matmul::T4", 4) } }

fn prepare(s: Matmul4T&) {}

fn run(s: Matmul4T&, iteration_id: i64) {
    reusable[] var pool: f64[>..] = [];
    let c = matmul_parallel(pool, s.base.n, 4);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul4T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}

struct Matmul8T { base: BaseMatmul }

fn new_matmul8t(pool: f64[>..]&) -> Matmul8T { Matmul8T { base: new_base(pool, "Matmul::T8", 8) } }

fn prepare(s: Matmul8T&) {}

fn run(s: Matmul8T&, iteration_id: i64) {
    reusable[] var pool: f64[>..] = [];
    let c = matmul_parallel(pool, s.base.n, 8);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul8T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}

struct Matmul16T { base: BaseMatmul }

fn new_matmul16t(pool: f64[>..]&) -> Matmul16T { Matmul16T { base: new_base(pool, "Matmul::T16", 16) } }

fn prepare(s: Matmul16T&) {}

fn run(s: Matmul16T&, iteration_id: i64) {
    reusable[] var pool: f64[>..] = [];
    let c = matmul_parallel(pool, s.base.n, 16);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul16T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}
