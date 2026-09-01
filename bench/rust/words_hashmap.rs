// Word frequency counting: see bench/goose/words.goose. `HashMap<&str, i32>`
// with `entry().or_insert()` is the Rust answer, and the keys borrow the text
// with no copy -- the idiomatic Rust row gets for free what the idiomatic C++
// row has to be talked out of. What it does pay for is std's default hasher:
// SipHash-1-3 is keyed and DoS-resistant by policy, which is the right default
// for a general map and the wrong one for counting words in a local buffer.
mod bench;
use bench::*;
use std::collections::HashMap;

const N: i64 = 500000;                   // BENCH_N
const MAXW: usize = 20;

fn main() {
    let n = N;
    let vocab = n / 10 + 16;
    let mut r: u64 = 12345;
    // A vocabulary of words, indexed randomly to build the text.
    let mut dict: Vec<String> = Vec::with_capacity(vocab as usize);
    for _ in 0..vocab {
        let mut w = String::with_capacity(MAXW);
        r = xs_next(r);
        let len = 3 + xs_mod(r, 12);
        for _ in 0..len {
            r = xs_next(r);
            w.push((b'a' + xs_mod(r, 26) as u8) as char);
        }
        dict.push(w);
    }
    // A text of N words drawn with a skew towards the front of the vocabulary.
    let mut text = String::new();
    for _ in 0..n {
        r = xs_next(r);
        let a = xs_mod(r, vocab);
        text.push_str(&dict[((a * a) / vocab) as usize]);
        text.push(' ');
    }
    let words: Vec<&str> = text.split_whitespace().collect();

    let mut map: HashMap<&str, i32> = HashMap::new();
    for w in &words { *map.entry(w).or_insert(0) += 1; }
    let distinct = map.len() as i64;
    let mut total: i64 = 0;
    for (k, c) in &map {
        total += *c as i64 * (k.len() as i64 + k.as_bytes()[0] as i64);
    }
    emit(words.len() as i64);
    emit(distinct);
    emit(total);
}
