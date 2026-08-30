// The same event log in fixed mode, which is the only mode Rust and C++ can
// express. Every element costs tag + the largest payload, and the string has
// to become a fixed-capacity buffer because fixed-mode payloads must be
// fixed-size. The workload, the RNG stream and the checksum are identical to
// records_var.goose -- the difference is purely representation, so the two
// rows isolate what variable-mode ADTs are worth.

import rng;

let N = 2000000;                         // BENCH_N
let PASSES = 4;

enum Event {
    Tick,
    Move { id: i32, dx: i16, dy: i16 },
    Say  { id: i32, text: u8[..15] },
    Quit { id: i32, code: u8 },
}

fn word(seed: int) -> u8[..15] {
    var w: u8[..15] = [];
    var r = xs_next(seed);
    let n = 4 + xs_mod(r, 12);
    for i in n {
        r = xs_next(r);
        w.push(('a' + xs_mod(r, 26)) as! u8);
    }
    return w;
}

fn main() {
    var log: Event[>..] = [];
    var r = 12345;
    for i in N {
        r = xs_next(r);
        let k = xs_mod(r, 8);
        let id = xs_mod(unsigned_shr(r, 3), 100000);
        if k < 4 {
            log.push(Event.Move { id: id as i32,
                                  dx: (xs_mod(unsigned_shr(r, 20), 201) - 100) as i16,
                                  dy: (xs_mod(unsigned_shr(r, 40), 201) - 100) as i16 });
        } else if k < 6 {
            log.push(Event.Say { id: id as i32, text: word(r) });
        } else if k < 7 {
            log.push(Event.Quit { id: id as i32, code: xs_mod(unsigned_shr(r, 20), 256) as u8 });
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
                Say s   => s.id + s.text.len + s.text[0],
                Quit q  => q.id + q.code,
            });
        }
    }
    print(log.len);
    print(total);
}
