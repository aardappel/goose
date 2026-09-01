// The same count in a hand-rolled open-addressed table over `&str` keys with an
// FNV hash: the direct analogue of the Goose version and of the C++ expert row,
// and what a Rust programmer writes once the profiler points at SipHash (the
// other answer being a third-party hasher crate, which this suite stays away
// from so every row is std only). Still entirely safe Rust; the difference from
// the HashMap row is the hash function and the probe, not the safety.
mod bench;
use bench::*;

const N: i64 = 500000;                   // BENCH_N
const MAXW: usize = 20;

fn fnv(s: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &c in s { h = (h ^ c as u64).wrapping_mul(0x100000001b3); }
    h
}

struct Slot<'a> { key: &'a str, count: i32 }

fn main() {
    let n = N;
    let vocab = n / 10 + 16;
    let mut r: u64 = 12345;
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
    let mut text = String::new();
    for _ in 0..n {
        r = xs_next(r);
        let a = xs_mod(r, vocab);
        text.push_str(&dict[((a * a) / vocab) as usize]);
        text.push(' ');
    }
    let words: Vec<&str> = text.split_whitespace().collect();

    let mut nslots: i64 = 16;
    while nslots < vocab * 4 { nslots *= 2; }
    let mask = (nslots - 1) as u64;
    let mut slots: Vec<Slot> = (0..nslots).map(|_| Slot { key: "", count: 0 }).collect();
    let mut distinct: i64 = 0;
    for w in &words {
        let mut idx = fnv(w.as_bytes()) & mask;
        loop {
            let s = &mut slots[idx as usize];
            if s.count == 0 { s.key = w; s.count = 1; distinct += 1; break; }
            if s.key == *w { s.count += 1; break; }
            idx = (idx + 1) & mask;
        }
    }
    let mut total: i64 = 0;
    for s in &slots {
        if s.count > 0 {
            total += s.count as i64 * (s.key.len() as i64 + s.key.as_bytes()[0] as i64);
        }
    }
    emit(words.len() as i64);
    emit(distinct);
    emit(total);
}
