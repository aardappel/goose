// An event log of variant records -- the shape of telemetry, game events, or
// any tagged message stream. In variable mode every event costs its own tag
// plus its own payload and nothing more, and the string payload is stored
// inline behind a one-byte varint length. Rust and C++ pad every element out
// to the largest variant plus alignment, and a std::string payload adds a
// separate allocation on top of that.
//
// The array is sequential (variable-size elements), so the scan is a walk,
// which is exactly what a log replay does anyway.

import rng;

let N = 2000000;                         // BENCH_N
let PASSES = 4;

enum Event {
    Tick,
    Move { id: i32, dx: i16, dy: i16 },
    Say  { id: i32, text: u8[varint] },
    Quit { id: i32, code: u8 },
}

// A word of 4..15 letters from its own chain, so the caller's stream advances
// once per event whatever the variant is.
fn word(seed: u64) -> u8[varint] {
    var w: u8[>..] = [];
    var r = xs_next(seed);
    let n = 4 + xs_mod(r, 12);
    for i in n {
        r = xs_next(r);
        w.push(('a' + xs_mod(r, 26)) as! u8);
    }
    return w;
}

fn main() {
    var log: Event..[>..] = [];
    var r: u64 = 12345;
    for i in N {
        r = xs_next(r);
        let k = xs_mod(r, 8);
        let id = xs_mod(r >> 3, 100000);
        if k < 4 {
            log.push(Event.Move { id: id as i32,
                                  dx: (xs_mod(r >> 20, 201) - 100) as i16,
                                  dy: (xs_mod(r >> 40, 201) - 100) as i16 });
        } else if k < 6 {
            log.push(Event.Say { id: id as i32, text: word(r) });
        } else if k < 7 {
            log.push(Event.Quit { id: id as i32, code: xs_mod(r >> 20, 256) as u8 });
        } else {
            log.push(Event.Tick);
        }
    }
    var total = 0;
    for p in PASSES {
        for e in log {
            total += (match e {
                Tick    => 1,
                Move m  => m.id + m.dx + m.dy,
                Say &s  => s.id + s.text.len + s.text[0],
                Quit q  => q.id + q.code,
            });
        }
    }
    print(log.len);
    print(total);
}
