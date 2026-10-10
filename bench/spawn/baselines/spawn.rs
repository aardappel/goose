// Thread spawn baselines in idiomatic Rust: std::thread::spawn with a `move`
// closure owning its payload (a cloned Vec / Vec<String>), joined; the fork-
// join tree uses std::thread::scope. Same command line and output as spawn.c
// (bench/spawn/design.md):
//
//   spawn_rs <mode> <a> <b> <target_ns> <batches>

use std::collections::VecDeque;
use std::hint::black_box;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::Instant;

static SINK: AtomicI64 = AtomicI64::new(0);

fn xs_next(mut x: u64) -> u64 {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    x
}

fn work(k: i64, seed: i64) -> i64 {
    let mut r = (seed as u64).wrapping_mul(2654435761).wrapping_add(1);
    for _ in 0..k {
        r = xs_next(r);
    }
    (r & 0xffff) as i64
}

fn spawn_args<const K: usize>(reps: i64) {
    let mut a = [0i64; K];
    for (i, x) in a.iter_mut().enumerate() {
        *x = i as i64;
    }
    for r in 0..reps {
        if K > 0 {
            a[0] = r;
        }
        let t = thread::spawn(move || {
            let v = if K > 0 { a[0] + a[K - 1] } else { 0 };
            SINK.store(v, Ordering::Relaxed);
        });
        t.join().unwrap();
    }
}

fn spawn_args_k(k: i64, reps: i64) -> bool {
    match k {
        0 => spawn_args::<0>(reps),
        1 => spawn_args::<1>(reps),
        2 => spawn_args::<2>(reps),
        4 => spawn_args::<4>(reps),
        8 => spawn_args::<8>(reps),
        16 => spawn_args::<16>(reps),
        32 => spawn_args::<32>(reps),
        64 => spawn_args::<64>(reps),
        _ => return false,
    }
    true
}

// Both children are scoped threads, joined as the scope ends.
fn tree_root(d: i64) {
    if d == 0 {
        return;
    }
    thread::scope(|s| {
        s.spawn(move || tree_root(d - 1));
        s.spawn(move || tree_root(d - 1));
    });
}

struct Pool {
    q: Mutex<(VecDeque<i64>, i64, bool)>,
    work: Condvar,
    done: Condvar,
}

struct Ctx {
    mode: String,
    a: i64,
    vsrc: Vec<i64>,
    ssrc: Vec<String>,
    pool: Arc<Pool>,
}

fn run(c: &Ctx, reps: i64) -> i64 {
    let a = c.a;
    match c.mode.as_str() {
        "empty" | "live" => {
            for i in 0..reps {
                thread::spawn(move || SINK.store(i, Ordering::Relaxed)).join().unwrap();
            }
            reps
        }
        "burst" => {
            let mut ts = Vec::with_capacity(a as usize);
            for _ in 0..reps {
                for i in 0..a {
                    ts.push(thread::spawn(move || SINK.store(i, Ordering::Relaxed)));
                }
                for t in ts.drain(..) {
                    t.join().unwrap();
                }
            }
            reps * a
        }
        "args" => {
            if spawn_args_k(a, reps) {
                reps
            } else {
                -1
            }
        }
        "bytes" => {
            for _ in 0..reps {
                let v = c.vsrc.clone();
                thread::spawn(move || {
                    let x = v.last().map_or(0, |l| l + v.len() as i64);
                    SINK.store(x, Ordering::Relaxed);
                })
                .join()
                .unwrap();
            }
            reps
        }
        "strings" => {
            for _ in 0..reps {
                let v = c.ssrc.clone();
                thread::spawn(move || SINK.store(v.len() as i64, Ordering::Relaxed))
                    .join()
                    .unwrap();
            }
            reps
        }
        "parallel" => {
            let ts: Vec<_> = (0..a)
                .map(|_| {
                    thread::spawn(move || {
                        for i in 0..reps {
                            thread::spawn(move || SINK.store(i, Ordering::Relaxed)).join().unwrap();
                        }
                    })
                })
                .collect();
            for t in ts {
                t.join().unwrap();
            }
            reps * a
        }
        "tree" => {
            for _ in 0..reps {
                thread::spawn(move || tree_root(a)).join().unwrap();
            }
            reps * (2i64 << a) - reps
        }
        "task_spawn" => {
            let mut i = 0;
            let mut ts = Vec::with_capacity(256);
            while i < reps {
                let n = (reps - i).min(256);
                for j in 0..n {
                    let s = i + j;
                    ts.push(thread::spawn(move || {
                        SINK.fetch_add(work(a, s), Ordering::Relaxed);
                    }));
                }
                for t in ts.drain(..) {
                    t.join().unwrap();
                }
                i += n;
            }
            reps
        }
        "task_pool" => {
            let p = &c.pool;
            let mut g = p.q.lock().unwrap();
            g.1 = reps;
            for i in 0..reps {
                g.0.push_back(i);
            }
            p.work.notify_all();
            let _g = p.done.wait_while(g, |g| g.1 != 0).unwrap();
            reps
        }
        "task_inline" => {
            for i in 0..reps {
                SINK.fetch_add(black_box(work(a, i)), Ordering::Relaxed);
            }
            reps
        }
        _ => -1,
    }
}

fn now_ns(t0: Instant) -> i64 {
    t0.elapsed().as_nanos() as i64
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 6 {
        eprintln!("usage: spawn_rs <mode> <a> <b> <target_ns> <batches>");
        std::process::exit(2);
    }
    let mode = args[1].clone();
    let a: i64 = args[2].parse().unwrap();
    let b: i64 = args[3].parse().unwrap();
    let target: i64 = args[4].parse().unwrap();
    let batches: i64 = args[5].parse().unwrap();
    let pool = Arc::new(Pool {
        q: Mutex::new((VecDeque::new(), 0, false)),
        work: Condvar::new(),
        done: Condvar::new(),
    });
    let c = Ctx {
        vsrc: if mode == "bytes" { (0..a / 8).collect() } else { Vec::new() },
        ssrc: if mode == "strings" {
            (0..a)
                .map(|i| std::iter::repeat((b'a' + (i % 26) as u8) as char).take(b as usize).collect())
                .collect()
        } else {
            Vec::new()
        },
        mode,
        a,
        pool: pool.clone(),
    };

    let idle_gate = Arc::new((Mutex::new(false), Condvar::new()));
    let mut idle = Vec::new();
    if c.mode == "live" {
        for _ in 0..b {
            let g = idle_gate.clone();
            idle.push(thread::spawn(move || {
                let (m, cv) = &*g;
                let _r = cv.wait_while(m.lock().unwrap(), |rel| !*rel).unwrap();
            }));
        }
    }
    if c.mode == "task_pool" {
        for _ in 0..b {
            let p = pool.clone();
            idle.push(thread::spawn(move || loop {
                let seed;
                {
                    let mut g = p.work.wait_while(p.q.lock().unwrap(), |g| !g.2 && g.0.is_empty()).unwrap();
                    if g.2 {
                        return;
                    }
                    seed = g.0.pop_front().unwrap();
                }
                let r = work(a, seed);
                let mut g = p.q.lock().unwrap();
                SINK.fetch_add(r, Ordering::Relaxed);
                g.1 -= 1;
                if g.1 == 0 {
                    p.done.notify_one();
                }
            }));
        }
    }

    if run(&c, 1) < 0 {
        println!("unsupported {}", c.mode);
        std::process::exit(3);
    }
    let t0 = Instant::now();
    let w0 = now_ns(t0);
    let mut i = 0;
    while i < 50 && now_ns(t0) - w0 < target / 4 {
        run(&c, 1);
        i += 1;
    }
    let mut reps = 1;
    loop {
        let t = now_ns(t0);
        run(&c, reps);
        if now_ns(t0) - t >= target {
            break;
        }
        reps *= 2;
    }
    for _ in 0..batches {
        let t = now_ns(t0);
        let units = run(&c, reps);
        println!("batch units={} ns={}", units, now_ns(t0) - t);
    }

    {
        let (m, cv) = &*idle_gate;
        *m.lock().unwrap() = true;
        cv.notify_all();
    }
    {
        pool.q.lock().unwrap().2 = true;
        pool.work.notify_all();
    }
    for t in idle {
        t.join().unwrap();
    }
}
