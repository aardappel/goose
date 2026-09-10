// The Mandelbrot set, rendered by a pool of worker threads. Goose threads
// share no memory: a worker is a separate program that receives flat
// values through typed queues and sends flat values back. Here the main
// program queues one job per row, spawns as many workers as the machine
// has hardware threads, and collects the finished rows -- a row is a
// struct holding its index and its pixels as an inline byte array -- in
// whatever order they arrive, then prints them in order. The single-thread
// timing is measured too, for the comparison.
//
// Build and run (from this directory):
//   goose -o mandelbrot_threads.c 23_mandelbrot_threads.goose && cl mandelbrot_threads.c && mandelbrot_threads

import std;
import os;

let W = 78;
let H = 36;
let MAXITER = 20000;

struct Job { y: i32 }                    // -1: no more work, exit
struct Row { y: i32, cells: u8[] }        // a flat value: it crosses the queue as one copy

// Iterations until |z| > 2, or MAXITER.
fn iterate(cx: f64, cy: f64) -> i64 {
    var x = 0.0;
    var y = 0.0;
    var i = 0;
    while i < MAXITER && x * x + y * y <= 4.0 {
        let nx = x * x - y * y + cx;
        y = 2.0 * x * y + cy;
        x = nx;
        i++;
    }
    return i;
}

fn render_row(y: i64) -> u8[>..] {
    let ramp = " .:-=+*#%@";
    var cells: u8[>..] = [];
    for x in W {
        let cx = -2.2 + 3.0 * x as f64 / W as f64;
        let cy = -1.2 + 2.4 * y as f64 / H as f64;
        let n = iterate(cx, cy);
        cells.push(if n == MAXITER { '@' } else { ramp[(n * 3) % (ramp.len - 1)] });
    }
    return cells;
}

// A worker: take a job, render it, send the row back; stop on y == -1.
thread_fn worker() {
    loop {
        let job = qget<Job>();
        guard job.y >= 0;
        qput(Row { y: job.y, cells: render_row(job.y) });   // one flat value, copied once into the queue
    }
}

fn main() {
    // Single-threaded first.
    let t0 = clock();
    var serial: u8[>..] = [];
    for y in H { serial.append(render_row(y)); }   // rows back to back, W bytes each
    let t1 = clock();

    // Then the pool. Jobs are queued before the workers start, one quit
    // job per worker after them, so every worker exits when the rows run out.
    let n = max(1, hardware_threads());
    for y in H { qput(Job { y: y as i32 }); }
    for i in n { qput(Job { y: -1 }); }
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    // Finished rows land in one byte buffer as they arrive; `order` maps a
    // row index to where its bytes went.
    var rows: u8[>..] = [];
    var order: i64[>..] = [];
    order.resize(H, -1);
    for done in H {
        let row = qget<Row>();
        order[row.y] = rows.len;
        rows.append(row.cells);
    }
    for id in ids { thread_wait(id); }
    let t2 = clock();

    // Rows arrived in whatever order the workers finished; print them in order.
    var same = true;
    for y in H {
        let k = order[y];
        let line = rows[k..k + W];
        print(line);
        if line != serial[y * W..(y + 1) * W] { same = false; }
    }
    print("rows match the serial render: ", same);
    var note: u8[>..] = [];
    format(note, "serial ");
    format_flt(note, (t1 - t0) * 1000.0, 1);
    format(note, " ms, ", n, " threads ");
    format_flt(note, (t2 - t1) * 1000.0, 1);
    format(note, " ms\n");
    write_stderr(note);
}
