// Names that C already uses, in every position a name reaches the generated
// code from. None of these needs avoiding: codegen suffixes every name the
// program chooses, so `random` becomes `random_g` and cannot be the `random`
// glibc declares. Without that, this file compiles on Windows and fails on
// Linux and macOS, which is exactly how it was found.
//
// The names are picked from the four ways C can take one away: keywords
// (`switch`, `unsigned`), functions declared by the headers the runtime
// includes (`random`, `printf`, `index`, `time`, `y1` -- the last three exist
// on the POSIX systems and not on Windows), object-like macros (`unix`, which
// gcc defines as 1), and function-like macros (`min` and `max` from windows.h,
// `isfinite` from math.h). `t0` and `L1` are the shapes codegen gives its own
// temporaries and labels.

struct Stats { min: i64, max: i64, unix: i64, data: i64, len: i64 }

enum Signal { time, index, unsigned { switch: i64 } }

let printf = 7;

fn random() -> i64 { return 3; }

fn memcpy(x: i64) -> i64 { return x + 1; }

fn isfinite(y1: i64, t0: i64) -> i64 { return y1 * t0; }

fn index(s: Stats&) -> i64 { return s.min + s.max + s.unix + s.data + s.len; }

fn main_cnames() {
    let t0 = random();                   // 3
    let L1 = memcpy(t0);                 // 4
    let volatile = isfinite(L1, 2);      // 8
    print(t0 + L1 + volatile);           // 15
    var st = Stats { min: 1, max: 2, unix: 3, data: 4, len: 5 };
    print(index(st));                    // 15
    print(printf);                       // 7
    var sig: Signal = Signal.unsigned { switch: 9 };
    match sig {
        time => print(0),
        index => print(1),
        unsigned u => print(u.switch),   // 9
    }
}

fn main() { main_cnames(); }
