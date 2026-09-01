// The same list, owning its strings: `Vec<String>`. This is the row that
// matches the Goose version's semantics -- a list that owns its bytes and can
// outlive the text it came from -- and it is what a Rust programmer reaches for
// the moment the list has to be stored, returned or sent anywhere. It costs one
// allocation per word and a 24-byte header per element, with no small-string
// optimisation to soften it, which is exactly the gap Goose's inline
// variable-size strings close.
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
    let words: Vec<String> = text.split_whitespace().map(String::from).collect();
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
