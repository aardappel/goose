// Conway's Game of Life on a torus. Two byte grids, one for the current
// generation and one for the next, and a step function that reads one and
// writes the other; the grids swap roles by swapping which one is passed
// where, so no cell is ever copied. Wrapping at the edges is `%`, which in
// Goose is Euclidean: (x - 1) % W is W - 1 when x is 0, with no special
// case. Prints a few generations of a glider and a blinker, then runs a
// larger random grid for a while and counts what is left.
//
// Build and run (from this directory):
//   goose -o life.c life.goose && cl life.c && life

import std;

fn neighbours(g: u8[:], w: i64, h: i64, x: i64, y: i64) -> i64 {
    var n = 0;
    for dy in -1..2 {
        for dx in -1..2 {
            if dx == 0 && dy == 0 { continue; }
            let nx = (x + dx) % w;
            let ny = (y + dy) % h;
            n += g[ny * w + nx];
        }
    }
    return n;
}

// One generation: `src` is read, `dst` written, both w*h cells.
fn step(src: u8[:], dst: u8[:], w: i64, h: i64) {
    for y in h {
        for x in w {
            let n = neighbours(src, w, h, x, y);
            let alive = src[y * w + x] == 1;
            dst[y * w + x] = if n == 3 || (alive && n == 2) { 1 } else { 0 };
        }
    }
}

fn show(g: u8[:], w: i64, h: i64) {
    var line: u8[>..] = [];
    for y in h {
        line.clear();
        for x in w { line.push(if g[y * w + x] == 1 { '#' } else { '.' }); }
        print(line);
    }
    print("");
}

fn population(g: u8[:]) -> i64 {
    var n = 0;
    for c in g { n += c; }
    return n;
}

fn main() {
    // A glider and a blinker on a small grid.
    let w = 12;
    let h = 8;
    var a: u8[>..] = [];
    var b: u8[>..] = [];
    a.resize(w * h, 0);
    b.resize(w * h, 0);
    for p in [[1, 0], [2, 1], [0, 2], [1, 2], [2, 2]] { a[p[1] * w + p[0]] = 1; }   // glider
    for x in 8..11 { a[5 * w + x] = 1; }                                        // blinker
    for round in 3 {
        print("generation ", round * 2, ", population ", population(a));
        show(a, w, h);
        step(a, b, w, h);                // a -> b
        print("generation ", round * 2 + 1, ", population ", population(b));
        show(b, w, h);
        step(b, a, w, h);                // b -> a: the roles swap, nothing is copied
    }

    // A random 64x32 soup, 200 generations.
    let W = 64;
    let H = 32;
    var r = rng { 2024 };
    var cur: u8[>..] = [];
    var next: u8[>..] = [];
    for i in W * H { cur.push(if r.rand_int(4) == 0 { 1 } else { 0 }); }
    next.resize(W * H, 0);
    print("soup: ", population(cur), " cells alive at start");
    for gen in 100 {
        step(cur, next, W, H);
        step(next, cur, W, H);
    }
    print("soup: ", population(cur), " cells alive after 200 generations");
    show(cur, W, H);
}
