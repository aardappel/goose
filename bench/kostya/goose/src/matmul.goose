// Matmul::Single, T4, T8, T16: the product of two n x n matrices, with the
// second one transposed first so both operands of every dot product are rows.

namespace matmul;

import std;
import slice_pool;
import bench;
import helper;

// A matrix is an array of rows. An array cannot hold growable arrays, so the
// rows are runs of a slice pool.
fn new_matrix(pool: slice_pool<f64>&, rows: i64, cols: i64) -> (f64[:])[>..] {
    var m: (f64[:])[>..] = [];
    for i in rows { m.push(pool.alloc_slice(cols)); }
    return m;
}

fn matgen(pool: slice_pool<f64>&, n: i64) -> (f64[:])[>..] {
    let tmp = 1.0 / (n * n) as f64;
    var a = new_matrix(pool, n, n);
    for i in n {
        for j in n { a[i][j] = tmp * (i - j) * (i + j); }
    }
    return a;
}

fn transpose(pool: slice_pool<f64>&, b: (f64[:])[:]) -> (f64[:])[>..] {
    let n = b.len;
    var b_t = new_matrix(pool, n, n);
    for i in n {
        for j in n {
            b_t[j][i] = b[i][j];
        }
    }
    return b_t;
}

fn matmul_sequential(pool: slice_pool<f64>&, a: (f64[:])[:], b: (f64[:])[:]) -> (f64[:])[>..] {
    let n = a.len;
    let b_t = transpose(pool, b);

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

// matmul_parallel transposes b as the reference does, once per run on the
// calling thread, and splits the columns of c evenly over the workers. Goose
// threads share no memory, so a split by rows, as the reference makes, would
// give every worker its own copy of all of b_t (16 x 6.5 MB at n = 900, which
// no longer fits the cache); split by columns, a worker gets just the rows of
// b_t its dot products read. Each worker holds its own copy of a, received
// once when the pool starts, and sends its columns of c back for
// matmul_parallel to assemble.
//
// A value crossing to another thread holds no slices, so a matrix travels as
// its rows back to back in one array, and the receiver makes rows over its
// copy.

struct Job { first: i64, last: i64, b_t: f64[] }       // b_t[first..last]; first < 0: stop
struct Columns { first: i64, last: i64, cells: f64[] } // row i: c[i][first..last]

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

thread_fn worker(a_cells: f64[], n: i64) {
    let a = rows_of(a_cells, n, n);

    loop {
        let job = qget<Job>();
        guard job.first >= 0 else { break; }

        let width = job.last - job.first;
        let b_t = rows_of(job.b_t, width, n);

        var pool = slice_pool<f64> {};
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

fn start_workers(a: (f64[:])[:], num_threads: i64) -> i64[>..] {
    var workers: i64[>..] = [];
    for t in num_threads { workers.push(thread_spawn(worker, flatten(a), a.len)); }
    return workers;
}

// The workers multiply by their own copies of a.
fn matmul_parallel(pool: slice_pool<f64>&, a: (f64[:])[:], b: (f64[:])[:], num_threads: i64) -> (f64[:])[>..] {
    let n = a.len;
    let b_t = transpose(pool, b);

    var c = new_matrix(pool, n, n);

    let columns_per_worker = (n + num_threads - 1) / num_threads;
    for worker_id in num_threads {
        let first = min(worker_id * columns_per_worker, n);
        let last = min(first + columns_per_worker, n);
        qput(Job { first: first, last: last, b_t: flatten(b_t[first..last]) });
    }

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
// threaded variants' workers start when it is made, with their copies of a.
struct BaseMatmul { n: i64, a: (f64[:])[], b: (f64[:])[], result_val: u32, workers: i64[] }

fn new_base(pool: slice_pool<f64>&, name: u8[:], num_threads: i64) -> BaseMatmul {
    let n = config_i64(name, "n");
    let a = matgen(pool, n);
    let b = matgen(pool, n);
    return BaseMatmul { n: n, a: a, b: b, result_val: 0, workers: start_workers(a, num_threads) };
}

// The checksum, which follows the last run, stops the workers, so that the
// next threaded benchmark's workers have the job queue to themselves.
fn stop_workers(base: BaseMatmul&) {
    for id in base.workers { qput(Job { first: -1, last: -1, b_t: [] }); }
    for id in base.workers { thread_wait(id); }
}

struct Matmul1T { base: BaseMatmul }

fn new_matmul1t(pool: slice_pool<f64>&) -> Matmul1T { Matmul1T { base: new_base(pool, "Matmul::Single", 0) } }

fn prepare(s: Matmul1T&) {}

fn run(s: Matmul1T&, iteration_id: i64) {
    var pool = slice_pool<f64> {};
    let c = matmul_sequential(pool, s.base.a, s.base.b);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul1T&) -> u32 { s.base.result_val }

struct Matmul4T { base: BaseMatmul }

fn new_matmul4t(pool: slice_pool<f64>&) -> Matmul4T { Matmul4T { base: new_base(pool, "Matmul::T4", 4) } }

fn prepare(s: Matmul4T&) {}

fn run(s: Matmul4T&, iteration_id: i64) {
    var pool = slice_pool<f64> {};
    let c = matmul_parallel(pool, s.base.a, s.base.b, 4);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul4T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}

struct Matmul8T { base: BaseMatmul }

fn new_matmul8t(pool: slice_pool<f64>&) -> Matmul8T { Matmul8T { base: new_base(pool, "Matmul::T8", 8) } }

fn prepare(s: Matmul8T&) {}

fn run(s: Matmul8T&, iteration_id: i64) {
    var pool = slice_pool<f64> {};
    let c = matmul_parallel(pool, s.base.a, s.base.b, 8);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul8T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}

struct Matmul16T { base: BaseMatmul }

fn new_matmul16t(pool: slice_pool<f64>&) -> Matmul16T { Matmul16T { base: new_base(pool, "Matmul::T16", 16) } }

fn prepare(s: Matmul16T&) {}

fn run(s: Matmul16T&, iteration_id: i64) {
    var pool = slice_pool<f64> {};
    let c = matmul_parallel(pool, s.base.a, s.base.b, 16);
    let center_value = c[s.base.n >> 1][s.base.n >> 1];
    s.base.result_val += checksum_f64(center_value);
}

fn checksum(s: Matmul16T&) -> u32 {
    s.base.stop_workers();
    return s.base.result_val;
}
