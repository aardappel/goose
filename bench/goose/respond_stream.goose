// The same responses as respond.goose, rendered straight into the output
// buffer as they are generated, with no response object in between: the
// shape of the expert C++ and Rust rows, so the gap between the two Goose
// rows is what building the DTO costs and the gap between this row and the
// other languages' streaming rows is the string builder alone. The RNG is
// consumed in exactly the order respond.goose consumes it, so the bytes --
// and the checksum -- are identical.

import rng;

let N = 400000;                          // BENCH_N

fn emit_int(out: u8[>..]&, v: i64) {
    if v == 0 { out.push('0'); return; }
    var d = v;
    var tmp: u8[..24] = [];
    while d > 0 { tmp.push(('0' + d % 10) as! u8); d /= 10; }
    var i = tmp.len;
    while i > 0 { i--; out.push(tmp[i] as u8); }
}

fn emit_name(out: u8[>..]&, seed: u64) {
    var r = xs_next(seed);
    let n = 3 + xs_mod(r, 8);
    let odd = xs_mod(r >> 8, 8) == 0;
    out.push('"');
    for i in n {
        r = xs_next(r);
        if odd && i == 1 {
            out.push('\\');
            out.push(if xs_mod(r, 2) == 0 { '"' } else { '\\' });
        } else { out.push(('a' + xs_mod(r, 26)) as! u8); }
    }
    out.push('"');
}

fn emit_sku(out: u8[>..]&, seed: u64) {
    var r = xs_next(seed);
    out.push('"');
    out.push(('A' + xs_mod(r, 26)) as! u8);
    out.push(('A' + xs_mod(r >> 8, 26)) as! u8);
    for i in 4 { r = xs_next(r); out.push(('0' + xs_mod(r, 10)) as! u8); }
    out.push('"');
}

fn fnv(s: u8[:]) -> u64 {
    var h = 0xcbf29ce484222325;
    for c in s { h = (h ^ c) * 0x100000001b3; }
    h
}

fn handle(seed: u64) -> u64, i64 {
    let r0 = xs_next(seed);
    let n = 1 + xs_mod(r0, 8);
    var out: u8[>..] = [];
    out.append("{\"id\":");
    emit_int(out, xs_mod(r0 >> 4, 1000000));
    out.append(",\"user\":");
    emit_name(out, r0 >> 8);
    out.append(",\"items\":[");
    var total = 0;
    var r = r0 >> 16;
    for i in n {
        r = xs_next(r);
        if i > 0 { out.push(','); }
        let qty = 1 + xs_mod(r >> 8, 5);
        let price = 100 + xs_mod(r >> 16, 99900);
        out.append("{\"sku\":");
        emit_sku(out, r);
        out.append(",\"qty\":");
        emit_int(out, qty);
        out.append(",\"price\":");
        emit_int(out, price);
        out.push('}');
        total += qty * price;
    }
    out.append("],\"total\":");
    emit_int(out, total);
    out.push('}');
    return fnv(out[..]), out.len;
}

fn main() {
    var seed: u64 = 12345;
    var h: u64 = 0;
    var bytes = 0;
    for i in N {
        seed = xs_next(seed);
        let hh, l = handle(seed);
        h = h ^ (hh * 31 + i as u64);
        bytes += l;
    }
    print(N);
    print(bytes);
    print(h);
}
