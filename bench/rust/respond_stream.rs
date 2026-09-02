// The same responses rendered straight into the output buffer as they are
// generated, with no response object in between: see
// bench/goose/respond_stream.goose. One `String` is reused across every request
// and only `clear()`ed between them, so after the first few requests the whole
// benchmark runs without touching the allocator at all, and the escaping is done
// as each character is produced rather than over a stored name.
//
// The RNG is consumed in exactly the order respond_dto.rs consumes it, so the
// bytes -- and the checksum -- are identical; the gap between the two rows is
// what building the DTO costs.
mod bench;
use bench::*;
use std::fmt::Write;

const N: i64 = 100000;                   // BENCH_N

// A user name of 3..10 letters; one in eight carries a quote or a backslash, and
// the backslash escape is written as the character is produced.
fn emit_name(out: &mut String, seed: u64) {
    let mut r = xs_next(seed);
    let n = 3 + xs_mod(r, 8);
    let odd = xs_mod(r >> 8, 8) == 0;
    out.push('"');
    for i in 0..n {
        r = xs_next(r);
        if odd && i == 1 {
            out.push('\\');
            out.push(if xs_mod(r, 2) == 0 { '"' } else { '\\' });
        } else { out.push((b'a' + xs_mod(r, 26) as u8) as char); }
    }
    out.push('"');
}

// A stock keeping unit: two letters and four digits.
fn emit_sku(out: &mut String, seed: u64) {
    let mut r = xs_next(seed);
    out.push('"');
    out.push((b'A' + xs_mod(r, 26) as u8) as char);
    out.push((b'A' + xs_mod(r >> 8, 26) as u8) as char);
    for _ in 0..4 { r = xs_next(r); out.push((b'0' + xs_mod(r, 10) as u8) as char); }
    out.push('"');
}

fn fnv(s: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &c in s { h = (h ^ c as u64).wrapping_mul(0x100000001b3); }
    h
}

fn handle(out: &mut String, seed: u64) -> (u64, i64) {
    let r0 = xs_next(seed);
    let n = 1 + xs_mod(r0, 8);
    out.clear();
    write!(out, "{{\"id\":{}", xs_mod(r0 >> 4, 1000000)).unwrap();
    out.push_str(",\"user\":");
    emit_name(out, r0 >> 8);
    out.push_str(",\"items\":[");
    let mut total: i64 = 0;
    let mut r = r0 >> 16;
    for i in 0..n {
        r = xs_next(r);
        if i > 0 { out.push(','); }
        let qty = 1 + xs_mod(r >> 8, 5);
        let price = 100 + xs_mod(r >> 16, 99900);
        out.push_str("{\"sku\":");
        emit_sku(out, r);
        write!(out, ",\"qty\":{},\"price\":{}}}", qty, price).unwrap();
        total += qty * price;
    }
    write!(out, "],\"total\":{}}}", total).unwrap();
    (fnv(out.as_bytes()), out.len() as i64)
}

fn main() {
    let mut out = String::new();
    let mut seed: u64 = 12345;
    let mut h: u64 = 0;
    let mut bytes: i64 = 0;
    for i in 0..N {
        seed = xs_next(seed);
        let (hh, l) = handle(&mut out, seed);
        h ^= hh.wrapping_mul(31).wrapping_add(i as u64);
        bytes += l;
    }
    emit(N);
    emit(bytes);
    println!("{}", h);
}
