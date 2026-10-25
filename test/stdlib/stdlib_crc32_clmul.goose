// std's crc32_u8, crc32_u64, clmul_lo and clmul_hi: the instructions where
// the target has them (arm64's crc32 and PMULL, x86-64's PCLMULQDQ in the
// simd fn versions), a table and shifts elsewhere. Every path must print
// the same, against bit-by-bit models written here; the runner builds this
// at each simd level beside the native and TinyCC runs.
import std;

// gzip's CRC-32 register a bit at a time.
fn crc_model(crc: u32, b: u8) -> u32 {
    var c = crc ^ b;
    for k in 8 { c = if c & 1 != 0 { 0xedb88320 ^ (c >> 1) } else { c >> 1 }; }
    return c;
}

// The carry-less product a bit at a time.
fn clmul_model(a: u64, b: u64) -> u64, u64 {
    var lo: u64 = 0;
    var hi: u64 = 0;
    for k: u64 in 0..64 {
        if (b >> k) & 1 != 0 {
            lo ^= a << k;
            if k != 0 { hi ^= a >> (64 - k); }
        }
    }
    return lo, hi;
}

// The same products in a simd fn, whose versions above the baseline take
// PCLMULQDQ on x86-64.
simd fn products(a: u64[:], b: u64[:], lo: u64[:], hi: u64[:]) {
    assert(a.len == b.len && lo.len == a.len && hi.len == a.len);
    for i in a.len {
        lo[i] = clmul_lo(a[i], b[i]);
        hi[i] = clmul_hi(a[i], b[i]);
    }
}

fn crcs() {
    // The CRC-32 check value of "123456789" (RFC 1952's polynomial).
    var c: u32 = 0xffffffff;
    for b in "123456789" { c = crc32_u8(c, b); }
    print(~c);                                                   // 3421780262 (0xcbf43926)
    c = 0xffffffff;
    let text = "12345678";
    c = crc32_u64(c, text[0] | text[1] as u64 << 8 | text[2] as u64 << 16 | text[3] as u64 << 24 |
                     text[4] as u64 << 32 | text[5] as u64 << 40 | text[6] as u64 << 48 | text[7] as u64 << 56);
    c = crc32_u8(c, '9');
    print(~c);                                                   // 3421780262
    var x: u64 = 0x9e3779b97f4a7c15;
    var bad = 0;
    for i in 1000 {
        x = x * 6364136223846793005 + 1442695040888963407;
        let r = (x >> 32) as! u32;
        var m = r;
        for k in 8 { m = crc_model(m, (x >> (8 * k)) as! u8); }
        if crc32_u64(r, x) != m { bad += 1; }
        if crc32_u8(r, x as! u8) != crc_model(r, x as! u8) { bad += 1; }
    }
    print(bad, " crc32 steps differ from the model");            // 0 crc32 steps differ from the model
}

fn products_check() {
    print(clmul_lo(3, 3), " ", clmul_hi(3, 3));                 // 5 0
    print(clmul_lo(0x8000000000000000, 2), " ", clmul_hi(0x8000000000000000, 2));    // 0 1
    print(clmul_hi(0xffffffffffffffff, 0xffffffffffffffff));     // 6148914691236517205 (0x5555...)
    var a: u64[>..] = [];
    var b: u64[>..] = [];
    var x: u64 = 7;
    for i in 1000 {
        x = x * 6364136223846793005 + 1442695040888963407;
        a.push(x);
        x = x * 6364136223846793005 + 1442695040888963407;
        b.push(if i % 7 == 0 { x >> (i % 64) } else { x });
    }
    var lo: u64[>..] = [];
    var hi: u64[>..] = [];
    lo.resize(a.len, 0);
    hi.resize(a.len, 0);
    products(a, b, lo, hi);
    var bad = 0;
    for i in a.len {
        let l, h = clmul_model(a[i], b[i]);
        if clmul_lo(a[i], b[i]) != l || clmul_hi(a[i], b[i]) != h { bad += 1; }
        if lo[i] != l || hi[i] != h { bad += 1; }
    }
    print(bad, " products differ from the model");              // 0 products differ from the model
}

fn main() {
    crcs();
    products_check();
}
