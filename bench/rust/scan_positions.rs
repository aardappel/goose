// The same position collection as bench/goose/scan.goose: what std Rust has
// for it is the iterator enumerate+filter positions, a byte-at-a-time filter; the memchr
// crate's iterator would be the batched answer, and this suite stays std only.
mod bench;
use bench::*;

const N: i64 = 200000;                   // BENCH_N

fn main() {
    let mut text: Vec<u8> = Vec::new();
    let mut r: u64 = 12345;
    for _ in 0..N {
        r = xs_next(r);
        let n = 30 + xs_mod(r, 60);
        for _ in 0..n {
            r = xs_next(r);
            text.push(b'a' + xs_mod(r, 26) as u8);
        }
        text.push(b'\n');
    }
    let mut pos: Vec<i64> = Vec::new();
    let mut total: i64 = 0;
    let mut npos = 0;
    for _ in 0..8 {
        pos.clear();
        pos.extend(text.iter().enumerate().filter(|&(_, &c)| c == b'\n').map(|(i, _)| i as i64));
        npos = pos.len();
        total += pos.iter().sum::<i64>();
    }
    emit(npos as i64);
    emit(total);
}
