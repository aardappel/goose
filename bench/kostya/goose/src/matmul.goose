// Matmul::Single, T4, T8, T16: the product of two n x n matrices, with the
// second one transposed first so both operands of every dot product are rows.

namespace matmul;

import std;
import bench;
import helper;

// An n x n matrix, row-major in one flat array.
fn matgen(n: i64) -> f64[>..] {
    let tmp = 1.0 / (n * n) as f64;
    var a: f64[>..] = [];
    for i in n {
        for j in n { a.push(tmp * (i - j) * (i + j)); }
    }
    return a;
}

// Row i of a matrix with rows of n elements.
fn row(m: f64[:], n: i64, i: i64) -> f64[:] { m[i * n..(i + 1) * n] }

fn dot(x: f64[:], y: f64[:]) -> f64 {
    assert(x.len == y.len);
    var s = 0.0;
    for k in x.len { s += x[k] * y[k]; }
    return s;
}

// Columns [first, last) of the n x n matrix b as the rows of a new matrix.
fn transpose_columns(b: f64[:], n: i64, first: i64, last: i64) -> f64[>..] {
    var bt: f64[>..] = [];
    bt.resize((last - first) * n, 0.0);
    for i in n {
        for x, j in row(b, n, i)[first..last] { bt[j * n + i] = x; }
    }
    return bt;
}

// Columns [first, last) of a * b, row by row.
fn multiply_columns(a: f64[:], b: f64[:], n: i64, first: i64, last: i64) -> f64[>..] {
    let b_t = transpose_columns(b, n, first, last);
    var c: f64[>..] = [];
    for i in n {
        let a_row = row(a, n, i);
        for j in last - first { c.push(dot(a_row, row(b_t, n, j))); }
    }
    return c;
}

fn center(c: f64[:], n: i64) -> f64 { c[(n >> 1) * n + (n >> 1)] }

fn single(b: Bench&) -> u32 {
    let n = config_i64(b.name, "n");
    let ma = matgen(n);
    let mb = matgen(n);
    var result: u32 = 0;
    measure(b) {
        let c = multiply_columns(ma, mb, n, 0, n);
        result += checksum_f64(center(c, n));
    };
    return result;
}

// --- the threaded variants ----------------------------------------------------

// Goose threads share no memory, so each worker holds its own copy of a and
// b, received once when the pool starts, as the other languages' benchmark
// objects hold theirs. The work is split by columns of c rather than by rows,
// so that no worker needs all of b_t: a job hands a worker a range of c's
// columns, it transposes just those columns of b, which are the rows of b_t
// its dot products read, and sends the finished columns back. Every run
// splits c's columns evenly over the workers, so that together they
// transpose all of b, and assembles all of c from their parts.

struct Job { first: i64, last: i64 }                     // first < 0: stop
struct Columns { first: i64, last: i64, cells: f64[] }   // c[i][first..last] for each row i

thread_fn worker(a: f64[], b: f64[], n: i64) {
    loop {
        let job = qget<Job>();
        guard job.first >= 0 else { break; }
        qput(Columns { first: job.first, last: job.last,
                       cells: multiply_columns(a, b, n, job.first, job.last) });
    }
}

fn parallel(b: Bench&, threads: i64) -> u32 {
    let n = config_i64(b.name, "n");
    let ma = matgen(n);
    let mb = matgen(n);
    var ids: i64[>..] = [];
    for t in threads { ids.push(thread_spawn(worker, copy(ma), copy(mb), n)); }
    let per_thread = (n + threads - 1) / threads;
    var result: u32 = 0;
    measure(b) {
        for t in threads {
            let first = min(t * per_thread, n);
            qput(Job { first: first, last: min(first + per_thread, n) });
        }
        var c: f64[>..] = [];
        c.resize(n * n, 0.0);
        for t in threads {
            let part = qget<Columns>();
            let w = part.last - part.first;
            for i in n {
                copy_into(row(c, n, i)[part.first..part.last], row(part.cells, w, i));
            }
        }
        result += checksum_f64(center(c, n));
    };
    for t in threads { qput(Job { first: -1, last: -1 }); }
    for id in ids { thread_wait(id); }
    return result;
}
