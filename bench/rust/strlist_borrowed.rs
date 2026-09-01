// A list of strings: see bench/goose/strlist.goose. Borrowing is the Rust
// default, so `split_whitespace().collect()` -- a `Vec<&str>` into the one text
// buffer, no copy and no per-word allocation -- is both the first thing a Rust
// programmer writes and the fast thing. The cost is in the type: the list
// borrows `text` and cannot outlive it, where the Goose list owns its bytes.
mod bench;
use bench::*;

const N: i64 = 200000;                   // BENCH_N
const PASSES: i64 = 4;

fn main() {
    let mut text = String::new();
    let mut r: u64 = 12345;
    for _ in 0..N {
        r = xs_next(r);
        let wl = 3 + xs_mod(r, 18);
        for _ in 0..wl {
            r = xs_next(r);
            text.push((b'a' + xs_mod(r, 26) as u8) as char);
        }
        text.push(' ');
    }
    let words: Vec<&str> = text.split_whitespace().collect();
    let mut total: i64 = 0;
    for _ in 0..PASSES {
        for w in &words {
            let b = w.as_bytes();
            total += b.len() as i64 + b[0] as i64 + b[b.len() - 1] as i64;
        }
    }
    emit(words.len() as i64);
    emit(total);
}
