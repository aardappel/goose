// A web service's hot loop: for each request, build the typed response
// object -- an id, a user name, a list of line items -- and render it as
// JSON into an output buffer, then throw both away. The response is a DTO
// with a string and a list of records in it, which is the shape Goose keeps
// in one contiguous block: the strings are inline behind varint lengths, the
// item list is inline in the struct, and each item's sku is inline in the
// item. The line items are produced by a function whose result is
// constructed straight into the field that receives it (spec 7.3), and both
// the response and its rendered text are locals of the handler, released by
// a pointer reset when it returns.
//
// The idiomatic C++ and Rust versions hold the same DTO as a struct with a
// std::string/String and a vector/Vec of items each owning a string: an
// allocation per string and per list, all freed per request. Their expert
// versions skip the object and stream the JSON straight into a reused
// buffer -- respond_stream.goose is the Goose row of that shape.

import rng;

let N = 400000;                          // BENCH_N

struct Item { sku: u8[varint], qty: varint, price: varint }
struct Resp { id: varint, user: u8[varint], items: Item[varint] }

// A user name of 3..10 letters; one in eight carries a quote or a backslash
// so the escaping path in the renderer is real.
fn name_of(seed: u64) -> u8[] {
    var s: u8[>..] = [];
    var r = xs_next(seed);
    let n = 3 + xs_mod(r, 8);
    let odd = xs_mod(r >> 8, 8) == 0;
    for i in n {
        r = xs_next(r);
        if odd && i == 1 { s.push(if xs_mod(r, 2) == 0 { '"' } else { '\\' }); }
        else { s.push(('a' + xs_mod(r, 26)) as! u8); }
    }
    return s;
}

// A stock keeping unit: two letters and four digits.
fn sku_of(seed: u64) -> u8[] {
    var s: u8[>..] = [];
    var r = xs_next(seed);
    s.push(('A' + xs_mod(r, 26)) as! u8);
    s.push(('A' + xs_mod(r >> 8, 26)) as! u8);
    for i in 4 { r = xs_next(r); s.push(('0' + xs_mod(r, 10)) as! u8); }
    return s;
}

fn make_items(seed: u64, n: i64) -> Item[] {
    var t: Item[>..] = [];
    var r = seed;
    for i in n {
        r = xs_next(r);
        t.push(Item { sku: sku_of(r), qty: 1 + xs_mod(r >> 8, 5), price: 100 + xs_mod(r >> 16, 99900) });
    }
    return t;
}

// --- rendering ---------------------------------------------------------------

fn emit_int(out: u8[>..]&, v: i64) {
    if v == 0 { out.push('0'); return; }
    var d = v;
    var tmp: u8[..24] = [];
    while d > 0 { tmp.push(('0' + d % 10) as! u8); d /= 10; }
    var i = tmp.len;
    while i > 0 { i--; out.push(tmp[i] as u8); }
}

fn emit_str(out: u8[>..]&, s: u8[:]) {
    out.push('"');
    for c in s {
        if c == '"' || c == '\\' { out.push('\\'); }
        out.push(c);
    }
    out.push('"');
}

fn render(out: u8[>..]&, r: Resp&) {
    out.append("{\"id\":");
    emit_int(out, r.id);
    out.append(",\"user\":");
    emit_str(out, r.user[..]);
    out.append(",\"items\":[");
    var total = 0;
    var first = true;
    for &it in r.items {
        if !first { out.push(','); }
        first = false;
        out.append("{\"sku\":");
        emit_str(out, it.sku[..]);
        out.append(",\"qty\":");
        emit_int(out, it.qty);
        out.append(",\"price\":");
        emit_int(out, it.price);
        out.push('}');
        total += it.qty * it.price;
    }
    out.append("],\"total\":");
    emit_int(out, total);
    out.push('}');
}

fn fnv(s: u8[:]) -> u64 {
    var h = 0xcbf29ce484222325;
    for c in s { h = (h ^ c) * 0x100000001b3; }
    h
}

// One request: the response object and its rendering are both locals.
fn handle(seed: u64) -> u64, i64 {
    let r = xs_next(seed);
    let n = 1 + xs_mod(r, 8);
    let resp = Resp { id: xs_mod(r >> 4, 1000000), user: name_of(r >> 8), items: make_items(r >> 16, n) };
    var out: u8[>..] = [];
    render(&out, &resp);
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
