recursive fn readonly(r: i64&, depth: i64) -> const i64& {
    if depth > 0 { return readonly(r, depth - 1); }
    let c: const i64& = r;
    return c;
}
// pick is part of walk's cycle, but no back edge uses pick's result while
// its returns are being checked, so they may differ in writability.
recursive fn walk(r: i64&, d: i64) -> i64 {
    if d <= 0 { return r; }
    let v .= pick(r, d);
    return v;
}
fn pick(r: i64&, d: i64) -> i64& {
    let n = walk(r, d - 1);
    if n > 100 { return r; }
    let c: const i64& = r;
    return c;
}
fn main() {
    var n = 7;
    let x .= readonly(n, 2);
    print(x);
    var m = 5;
    print(walk(m, 3));
}
