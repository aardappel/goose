// A back edge is given only what the returns checked or predicted before it
// guarantee. Where neither pins its result down, the returns checked after it
// may differ in writability or exactness.

// Overloading id keeps the prediction from resolving pick's returns.
fn id(x: i64&) -> i64& { return x; }
fn id(x: u8&) -> u8& { return x; }
recursive fn pick(r: i64&, d: i64) -> i64& {
    if d > 0 { let v = pick(r, d - 1); print(v); }
    if d > 1 { return id(r); }
    let c: const i64& = r;
    return c;
}

// Holder results are not predicted: an exact holder, then one read back out
// of the container.
struct H { value: i64&, k: i64 }
recursive fn walk(hs: H[>..]&, d: i64) -> H {
    if d > 0 { let x = walk(hs, d - 1); print(x.k); }
    if d > 1 { return H { value: hs[0].k, k: 1 }; }
    return hs[1];
}

fn main() {
    var n = 1;
    let x = pick(n, 3);
    print(x);
    var g = 7;
    var hs: H[>..] = [];
    hs.push(H { value: g, k: 10 });
    hs.push(H { value: g, k: 20 });
    let h = walk(hs, 3);
    print(h.value);
    print(h.k);
}
